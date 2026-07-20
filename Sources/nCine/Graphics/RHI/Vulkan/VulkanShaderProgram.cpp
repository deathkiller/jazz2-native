#include "VulkanShaderProgram.h"
#include "VulkanDevice.h"
#include "VulkanBufferObject.h"
#include "VulkanCommon.h"
#include "../../RenderResources.h"

#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"

#include <cstring>

namespace nCine::RHI::Vulkan
{
	namespace
	{
		// std140 base alignment and size (unpadded) of a scalar/vector/matrix uniform type
		void Std140BaseLayout(ShaderCompiler::UniformType type, std::uint32_t& align, std::uint32_t& size)
		{
			switch (type) {
				case ShaderCompiler::UniformType::Float:
				case ShaderCompiler::UniformType::Int:
				case ShaderCompiler::UniformType::UInt:
				case ShaderCompiler::UniformType::Bool: align = 4; size = 4; break;
				case ShaderCompiler::UniformType::Vec2:
				case ShaderCompiler::UniformType::IVec2:
				case ShaderCompiler::UniformType::UVec2:
				case ShaderCompiler::UniformType::BVec2: align = 8; size = 8; break;
				case ShaderCompiler::UniformType::Vec3:
				case ShaderCompiler::UniformType::IVec3:
				case ShaderCompiler::UniformType::UVec3:
				case ShaderCompiler::UniformType::BVec3: align = 16; size = 12; break;
				case ShaderCompiler::UniformType::Vec4:
				case ShaderCompiler::UniformType::IVec4:
				case ShaderCompiler::UniformType::UVec4:
				case ShaderCompiler::UniformType::BVec4: align = 16; size = 16; break;
				case ShaderCompiler::UniformType::Mat2: align = 16; size = 32; break;
				case ShaderCompiler::UniformType::Mat3: align = 16; size = 48; break;
				case ShaderCompiler::UniformType::Mat4: align = 16; size = 64; break;
				default: align = 16; size = 16; break;
			}
		}

		std::uint32_t RoundUp(std::uint32_t value, std::uint32_t alignment)
		{
			return (alignment == 0) ? value : ((value + alignment - 1) / alignment) * alignment;
		}
	}

	std::uint32_t VulkanShaderProgram::nextHandle_ = 1;

	VulkanShaderProgram::VulkanShaderProgram()
		: VulkanShaderProgram(QueryPhase::Immediate)
	{
	}

	VulkanShaderProgram::VulkanShaderProgram(QueryPhase queryPhase)
		: handle_(nextHandle_++), status_(Status::NotLinked), introspection_(Introspection::Disabled), queryPhase_(queryPhase),
			batchSize_(DefaultBatchSize), shouldLogOnErrors_(true), uniformsSize_(0), uniformBlocksSize_(0),
			reflection_(nullptr), boundVbo_(nullptr), boundIbo_(nullptr),
			vsModule_(0), fsModule_(0), descriptorSetLayout_(0), pipelineLayout_(0), globalsSize_(0)
	{
	}

	VulkanShaderProgram::VulkanShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase)
		: VulkanShaderProgram(queryPhase)
	{
		static_cast<void>(vertexFile);
		static_cast<void>(fragmentFile);
		static_cast<void>(introspection);
	}

	VulkanShaderProgram::VulkanShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection)
		: VulkanShaderProgram(vertexFile, fragmentFile, introspection, QueryPhase::Immediate)
	{
	}

	VulkanShaderProgram::VulkanShaderProgram(StringView vertexFile, StringView fragmentFile)
		: VulkanShaderProgram(vertexFile, fragmentFile, Introspection::Enabled, QueryPhase::Immediate)
	{
	}

	VulkanShaderProgram::~VulkanShaderProgram()
	{
		// Drop any cached pipelines keyed on this program, then destroy the modules / layouts. The device
		// waits for the GPU to go idle on teardown, so destroying here is safe.
		VulkanDevice::OnShaderProgramDestroyed(this);
		VkDevice device = VkDeviceHandle();
		if (device != VK_NULL_HANDLE) {
			if (pipelineLayout_ != 0) { vkDestroyPipelineLayout(device, reinterpret_cast<VkPipelineLayout>(pipelineLayout_), nullptr); }
			if (descriptorSetLayout_ != 0) { vkDestroyDescriptorSetLayout(device, reinterpret_cast<VkDescriptorSetLayout>(descriptorSetLayout_), nullptr); }
			if (vsModule_ != 0) { vkDestroyShaderModule(device, reinterpret_cast<VkShaderModule>(vsModule_), nullptr); }
			if (fsModule_ != 0) { vkDestroyShaderModule(device, reinterpret_cast<VkShaderModule>(fsModule_), nullptr); }
		}
		vsModule_ = fsModule_ = descriptorSetLayout_ = pipelineLayout_ = 0;
		// The pipeline keys per-shader camera uniform data on the program pointer; drop this program's
		// entry so RenderResources::Dispose() finds the map empty (mirrors GLShaderProgram's destructor)
		RenderResources::RemoveCameraUniformData(this);
	}

	bool VulkanShaderProgram::IsLinked() const
	{
		return (status_ == Status::Linked || status_ == Status::LinkedWithDeferredQueries || status_ == Status::LinkedWithIntrospection);
	}

	bool VulkanShaderProgram::AttachShaderFromFile(ShaderStage stage, StringView filename)
	{
		static_cast<void>(stage);
		static_cast<void>(filename);
		return true;
	}

	bool VulkanShaderProgram::AttachShaderFromString(ShaderStage stage, StringView string)
	{
		static_cast<void>(stage);
		static_cast<void>(string);
		return true;
	}

	bool VulkanShaderProgram::AttachShaderFromStrings(ShaderStage stage, ArrayView<const StringView> strings)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		return true;
	}

	bool VulkanShaderProgram::AttachShaderFromStringsAndFile(ShaderStage stage, ArrayView<const StringView> strings, StringView filename)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		static_cast<void>(filename);
		return true;
	}

	bool VulkanShaderProgram::Link(Introspection introspection)
	{
		return FinalizeAfterLinking(introspection);
	}

	bool VulkanShaderProgram::FinalizeAfterLinking(Introspection introspection)
	{
		introspection_ = introspection;
		PerformIntrospection();
		return true;
	}

	void VulkanShaderProgram::Use()
	{
		VulkanDevice::BindProgram(this);
	}

	void VulkanShaderProgram::PerformIntrospection()
	{
		if (introspection_ != Introspection::Disabled && status_ != Status::LinkedWithIntrospection) {
			uniformsSize_ = 0;
			uniformBlocksSize_ = 0;

			if (reflection_ != nullptr) {
				ImportReflection();
				// Build the SPIR-V modules, descriptor-set layout, pipeline layout and gather metadata while
				// the reflection (and its embedded SPIR-V) is still available.
				BuildPipelineState();
			}
			status_ = Status::LinkedWithIntrospection;
		}
		// The reflection is consumed by introspection (its layout is imported into the members above)
		reflection_ = nullptr;
	}

	void VulkanShaderProgram::BuildPipelineState()
	{
		const ShaderCompiler::ProgramVariant& reflection = *reflection_;
		VkDevice device = VkDeviceHandle();
		if (device == VK_NULL_HANDLE) {
			LOGE("Cannot build Vulkan pipeline state: device is not created yet");
			return;
		}
		if (reflection.VkVsSpirv == nullptr || reflection.VkFsSpirv == nullptr ||
			reflection.VkVsSpirvSize == 0 || reflection.VkFsSpirvSize == 0) {
			// No SPIR-V (e.g. a runtime-compiled shader outside the emitter's subset); draws are skipped
			if (shouldLogOnErrors_) {
				LOGW("Program {} has no SPIR-V modules; draws with it are skipped", handle_);
			}
			return;
		}

		// -- Shader modules (just wrap the pre-compiled SPIR-V words) --
		auto createModule = [device](const std::uint32_t* words, std::size_t wordCount) -> std::uint64_t {
			VkShaderModuleCreateInfo smci = {};
			smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			smci.codeSize = wordCount * sizeof(std::uint32_t);
			smci.pCode = words;
			VkShaderModule module = VK_NULL_HANDLE;
			if (vkCreateShaderModule(device, &smci, nullptr, &module) != VK_SUCCESS) {
				return 0;
			}
			return reinterpret_cast<std::uint64_t>(module);
		};
		vsModule_ = createModule(reflection.VkVsSpirv, reflection.VkVsSpirvSize);
		fsModule_ = createModule(reflection.VkFsSpirv, reflection.VkFsSpirvSize);
		if (vsModule_ == 0 || fsModule_ == 0) {
			LOGE("Failed to create Vulkan shader modules for program {}", handle_);
			status_ = Status::CompilationFailed;
			return;
		}

		// -- Descriptor bindings, in the offline emitter's scheme (set 0: optional _Globals UBO at binding 0, then the
		// std140 blocks at uboBase+i, then the combined image samplers at uboBase+BlockCount+j). --
		const bool hasGlobals = (reflection.UniformCount > 0);
		const std::uint32_t uboBase = (hasGlobals ? 1u : 0u);
		std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
		descriptorBindings_.clear();
		looseUniforms_.clear();
		globalsSize_ = 0;

		const VkShaderStageFlags allStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		if (hasGlobals) {
			// Gather the loose uniforms into a std140 _Globals block layout (matches the offline emitter)
			std::uint32_t offset = 0;
			for (std::size_t i = 0; i < reflection.UniformCount; i++) {
				const ShaderCompiler::Uniform& u = reflection.Uniforms[i];
				std::uint32_t baseAlign = 0, baseSize = 0;
				Std140BaseLayout(u.Type, baseAlign, baseSize);
				std::uint32_t align = baseAlign, size = baseSize;
				if (u.ArraySize > 0) {
					align = RoundUp(baseAlign, 16);
					const std::uint32_t stride = RoundUp(baseSize, 16);
					size = stride * u.ArraySize;
				}
				offset = RoundUp(offset, align);
				looseUniforms_.push_back({ String(u.Name), offset, size });
				offset += size;
			}
			globalsSize_ = RoundUp(offset, 16);

			VkDescriptorSetLayoutBinding b = {};
			b.binding = 0;
			b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			b.descriptorCount = 1;
			b.stageFlags = allStages;
			layoutBindings.push_back(b);
			descriptorBindings_.push_back({ DescriptorBinding::Kind::Globals, 0, -1 });
		}

		for (std::size_t i = 0; i < reflection.BlockCount; i++) {
			const std::uint32_t binding = uboBase + std::uint32_t(i);
			VkDescriptorSetLayoutBinding b = {};
			b.binding = binding;
			b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			b.descriptorCount = 1;
			b.stageFlags = allStages;
			layoutBindings.push_back(b);
			// The device records block i's bound range in boundUniformRanges_[i] (see VulkanShaderUniformBlocks)
			descriptorBindings_.push_back({ DescriptorBinding::Kind::Block, binding, std::int32_t(i) });
		}

		for (std::size_t j = 0; j < reflection.TextureCount; j++) {
			const std::uint32_t binding = uboBase + std::uint32_t(reflection.BlockCount) + std::uint32_t(j);
			VkDescriptorSetLayoutBinding b = {};
			b.binding = binding;
			b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			b.descriptorCount = 1;
			b.stageFlags = allStages;
			layoutBindings.push_back(b);
			const std::int32_t unit = (reflection.Textures[j].Unit >= 0 ? reflection.Textures[j].Unit : std::int32_t(j));
			descriptorBindings_.push_back({ DescriptorBinding::Kind::Sampler, binding, unit });
		}

		VkDescriptorSetLayoutCreateInfo dslci = {};
		dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		dslci.bindingCount = std::uint32_t(layoutBindings.size());
		dslci.pBindings = layoutBindings.empty() ? nullptr : layoutBindings.data();
		VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
		if (vkCreateDescriptorSetLayout(device, &dslci, nullptr, &setLayout) != VK_SUCCESS) {
			LOGE("Failed to create Vulkan descriptor set layout for program {}", handle_);
			status_ = Status::CompilationFailed;
			return;
		}
		descriptorSetLayout_ = reinterpret_cast<std::uint64_t>(setLayout);

		VkPipelineLayoutCreateInfo plci = {};
		plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		plci.setLayoutCount = 1;
		plci.pSetLayouts = &setLayout;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		if (vkCreatePipelineLayout(device, &plci, nullptr, &layout) != VK_SUCCESS) {
			LOGE("Failed to create Vulkan pipeline layout for program {}", handle_);
			status_ = Status::CompilationFailed;
			return;
		}
		pipelineLayout_ = reinterpret_cast<std::uint64_t>(layout);
	}

	std::uint32_t VulkanShaderProgram::GetVertexInput(VertexAttrib* outAttribs, std::uint32_t maxAttribs, std::uint32_t& outStride) const
	{
		std::uint32_t count = 0;
		outStride = 0;
		for (const VulkanAttribute& a : attributes_) {
			const std::int32_t loc = a.GetLocation();
			if (loc < 0) {
				continue;
			}
			const VulkanVertexFormat::Attribute& fa = vertexFormat_[std::uint32_t(loc)];
			if (!fa.IsEnabled()) {
				continue;
			}
			if (count < maxAttribs) {
				VertexAttrib& attr = outAttribs[count++];
				attr.Location = std::uint32_t(loc);
				attr.ComponentCount = fa.GetSize();
				attr.Offset = std::uint32_t(reinterpret_cast<std::uintptr_t>(fa.GetPointer()));
				attr.Type = fa.GetType();
				attr.Normalized = fa.IsNormalized();
			}
			if (fa.GetStride() > 0) {
				outStride = std::uint32_t(fa.GetStride());
			}
		}
		return count;
	}

	void VulkanShaderProgram::ImportReflection()
	{
		const ShaderCompiler::ProgramVariant& reflection = *reflection_;
		std::int32_t nextLocation = 0;

		// Loose uniforms - samplers are kept in a separate reflection list but treated as loose uniforms here
		for (std::size_t i = 0; i < reflection.UniformCount; i++) {
			const ShaderCompiler::Uniform& u = reflection.Uniforms[i];
			uniforms_.emplace_back(this, u.Name, u.Type, std::int32_t(u.ArraySize), nextLocation++);
			uniformsSize_ += uniforms_.back().GetMemorySize();
		}
		for (std::size_t i = 0; i < reflection.TextureCount; i++) {
			const ShaderCompiler::TextureBinding& t = reflection.Textures[i];
			uniforms_.emplace_back(this, t.Name, ShaderCompiler::UniformType::Sampler2D, 1, nextLocation++);
			uniformsSize_ += uniforms_.back().GetMemorySize();
		}

		uniformBlocks_.reserve(reflection.BlockCount);
		for (std::size_t i = 0; i < reflection.BlockCount; i++) {
			const ShaderCompiler::UniformBlock& b = reflection.Blocks[i];

			// A BATCH_SIZE-sized instance array uses the explicitly set batch size, or the same 64 KB-based
			// fallback the in-shader "#ifndef BATCH_SIZE" defaults assume when no size is injected
			std::uint32_t effectiveBatchSize = 0;
			std::uint32_t dataSize = b.BaseSize;
			if (b.InstanceStride > 0) {
				effectiveBatchSize = (batchSize_ != std::uint32_t(DefaultBatchSize) && batchSize_ > 0)
					? batchSize_ : (64u * 1024u) / b.InstanceStride;
				dataSize += b.InstanceStride * effectiveBatchSize;
			}

			const std::uint32_t blockIndex = std::uint32_t(uniformBlocks_.size());
			uniformBlocks_.emplace_back(blockIndex, b.Name, std::int32_t(dataSize));
			VulkanUniformBlock& block = uniformBlocks_.back();
			uniformBlocksSize_ += block.GetSize();

			if (introspection_ != Introspection::NoUniformsInBlocks) {
				block.members_.reserve(b.MemberCount);
				for (std::size_t j = 0; j < b.MemberCount; j++) {
					const ShaderCompiler::BlockMember& m = b.Members[j];
					if (m.Type == ShaderCompiler::UniformType::Struct) {
						// Struct aggregates are never read by name; only flat leaf members matter
						continue;
					}
					VulkanUniform member;
					member.SetName(m.Name);
					member.type_ = m.Type;
					member.size_ = (m.ArraySize == ShaderCompiler::SymbolicArraySize)
						? std::int32_t(effectiveBatchSize)
						: (m.ArraySize > 0 ? std::int32_t(m.ArraySize) : 1);
					member.blockIndex_ = std::int32_t(blockIndex);
					member.offset_ = std::int32_t(m.Offset);
					member.owner_ = this;
					block.members_.push_back(member);
				}
			}
		}

		for (std::size_t i = 0; i < reflection.AttributeCount; i++) {
			const ShaderCompiler::Attribute& a = reflection.Attributes[i];
			const std::int32_t location = (a.Location >= 0 ? a.Location : std::int32_t(i));
			attributes_.emplace_back(a.Name, a.Type, location);
			vertexFormat_[std::uint32_t(location)].Init(std::uint32_t(location), std::int32_t(UniformTypeInfo::ComponentCount(a.Type)), 0);
		}
	}

	bool VulkanShaderProgram::HasAttribute(const char* name) const
	{
		for (const VulkanAttribute& a : attributes_) {
			if (std::strcmp(a.GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	VulkanVertexFormat::Attribute* VulkanShaderProgram::GetAttribute(const char* name)
	{
		for (const VulkanAttribute& a : attributes_) {
			if (std::strcmp(a.GetName(), name) == 0 && a.GetLocation() >= 0) {
				return &vertexFormat_[std::uint32_t(a.GetLocation())];
			}
		}
		return nullptr;
	}

	void VulkanShaderProgram::DefineVertexFormat(const VulkanBufferObject* vbo, const VulkanBufferObject* ibo, std::uint32_t vboOffset)
	{
		boundVbo_ = vbo;
		boundIbo_ = ibo;
		if (vbo != nullptr) {
			for (const VulkanAttribute& a : attributes_) {
				if (a.GetLocation() >= 0) {
					VulkanVertexFormat::Attribute& attr = vertexFormat_[std::uint32_t(a.GetLocation())];
					attr.setVbo(vbo);
					attr.SetBaseOffset(vboOffset);
				}
			}
			vertexFormat_.SetIbo(ibo);
		}
	}

	void VulkanShaderProgram::Reset()
	{
		uniforms_.clear();
		uniformBlocks_.clear();
		attributes_.clear();
		resolvedUniforms_.clear();
		vertexFormat_.Reset();

		VulkanDevice::OnShaderProgramDestroyed(this);
		VkDevice device = VkDeviceHandle();
		if (device != VK_NULL_HANDLE) {
			if (pipelineLayout_ != 0) { vkDestroyPipelineLayout(device, reinterpret_cast<VkPipelineLayout>(pipelineLayout_), nullptr); }
			if (descriptorSetLayout_ != 0) { vkDestroyDescriptorSetLayout(device, reinterpret_cast<VkDescriptorSetLayout>(descriptorSetLayout_), nullptr); }
			if (vsModule_ != 0) { vkDestroyShaderModule(device, reinterpret_cast<VkShaderModule>(vsModule_), nullptr); }
			if (fsModule_ != 0) { vkDestroyShaderModule(device, reinterpret_cast<VkShaderModule>(fsModule_), nullptr); }
		}
		vsModule_ = fsModule_ = descriptorSetLayout_ = pipelineLayout_ = 0;
		descriptorBindings_.clear();
		looseUniforms_.clear();
		globalsSize_ = 0;

		status_ = Status::NotLinked;
		batchSize_ = DefaultBatchSize;
		reflection_ = nullptr;
	}

	void VulkanShaderProgram::SetObjectLabel(StringView label)
	{
		// The backend selects the SPIR-V variant from the reflection, not the label, so this is informational only
		static_cast<void>(label);
	}

	void VulkanShaderProgram::SetResolvedUniform(const char* name, const std::uint8_t* data)
	{
		for (ResolvedUniform& r : resolvedUniforms_) {
			if (r.Name == name) {
				r.Data = data;
				return;
			}
		}
		resolvedUniforms_.push_back({name, data});
	}

	const std::uint8_t* VulkanShaderProgram::ResolveUniform(const char* name) const
	{
		for (const ResolvedUniform& r : resolvedUniforms_) {
			if (r.Name == name) {
				return r.Data;
			}
		}
		return nullptr;
	}
}
