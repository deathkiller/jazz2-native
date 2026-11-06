// metal-cpp is included FIRST: the contract headers below pull in Death::Containers with a global using-directive,
// and metal-cpp's own NS::String / MTL::Function would otherwise be shadowed by that namespace's String and
// Function templates while its headers are parsed
#include "MetalCommon.h"

#include "MetalShaderProgram.h"
#include "MetalDevice.h"
#include "MetalBufferObject.h"
#include "../../RenderResources.h"

#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"

#include <cstring>

namespace nCine::RHI::Metal
{
	namespace
	{
		// std140 base alignment and size (unpadded) of a scalar/vector/matrix uniform type - the layout the offline
		// MSL emitter gives the gathered "_Globals" struct, so the bytes copied into the buffer land where the
		// shader reads them
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

		/** Compiles one MSL stage into a library and fetches its entry function; both are owned by the caller */
		bool CompileStage(MTL::Device* device, const char* source, const char* entryPoint, std::uint32_t programHandle,
			MTL::Library*& outLibrary, MTL::Function*& outFunction)
		{
			outLibrary = nullptr;
			outFunction = nullptr;
			NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

			MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
			options->setFastMathEnabled(true);
			NS::Error* error = nullptr;
			MTL::Library* library = device->newLibrary(NS::String::string(source, NS::UTF8StringEncoding), options, &error);
			options->release();
			if (library == nullptr) {
				LOGE("MSL {} compile failed for program {}: {}", entryPoint, programHandle,
					(error != nullptr && error->localizedDescription() != nullptr) ? error->localizedDescription()->utf8String() : "unknown error");
				pool->release();
				return false;
			}
			MTL::Function* function = library->newFunction(NS::String::string(entryPoint, NS::UTF8StringEncoding));
			if (function == nullptr) {
				LOGE("MSL entry point {} not found in program {}", entryPoint, programHandle);
				library->release();
				pool->release();
				return false;
			}
			outLibrary = library;
			outFunction = function;
			pool->release();
			return true;
		}
	}

	std::uint32_t MetalShaderProgram::_nextHandle = 1;

	MetalShaderProgram::MetalShaderProgram()
		: MetalShaderProgram(QueryPhase::Immediate)
	{
	}

	MetalShaderProgram::MetalShaderProgram(QueryPhase queryPhase)
		: _handle(_nextHandle++), _status(Status::NotLinked), _introspection(Introspection::Disabled), _queryPhase(queryPhase),
			_batchSize(DefaultBatchSize), _shouldLogOnErrors(true), _uniformsSize(0), _uniformBlocksSize(0),
			_reflection(nullptr), _boundVbo(nullptr), _boundIbo(nullptr),
			_vsLibrary(nullptr), _fsLibrary(nullptr), _vsFunction(nullptr), _fsFunction(nullptr), _globalsSize(0)
	{
	}

	MetalShaderProgram::MetalShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase)
		: MetalShaderProgram(queryPhase)
	{
		static_cast<void>(vertexFile);
		static_cast<void>(fragmentFile);
		static_cast<void>(introspection);
	}

	MetalShaderProgram::MetalShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection)
		: MetalShaderProgram(vertexFile, fragmentFile, introspection, QueryPhase::Immediate)
	{
	}

	MetalShaderProgram::MetalShaderProgram(StringView vertexFile, StringView fragmentFile)
		: MetalShaderProgram(vertexFile, fragmentFile, Introspection::Enabled, QueryPhase::Immediate)
	{
	}

	MetalShaderProgram::~MetalShaderProgram()
	{
		// Drop the device's cached pipelines keyed on this program, then the functions / libraries. A pipeline
		// still referenced by an in-flight command buffer stays alive through that reference.
		MetalDevice::OnShaderProgramDestroyed(this);
		ReleaseGpu();
		// The pipeline keys per-shader camera uniform data on the program pointer; drop this program's
		// entry so RenderResources::Dispose() finds the map empty (mirrors GLShaderProgram's destructor)
		RenderResources::RemoveCameraUniformData(this);
	}

	void MetalShaderProgram::ReleaseGpu()
	{
		if (_vsFunction != nullptr) { static_cast<MTL::Function*>(_vsFunction)->release(); _vsFunction = nullptr; }
		if (_fsFunction != nullptr) { static_cast<MTL::Function*>(_fsFunction)->release(); _fsFunction = nullptr; }
		if (_vsLibrary != nullptr) { static_cast<MTL::Library*>(_vsLibrary)->release(); _vsLibrary = nullptr; }
		if (_fsLibrary != nullptr) { static_cast<MTL::Library*>(_fsLibrary)->release(); _fsLibrary = nullptr; }
	}

	bool MetalShaderProgram::IsLinked() const
	{
		return (_status == Status::Linked || _status == Status::LinkedWithDeferredQueries || _status == Status::LinkedWithIntrospection);
	}

	bool MetalShaderProgram::AttachShaderFromFile(ShaderStage stage, StringView filename)
	{
		static_cast<void>(stage);
		static_cast<void>(filename);
		return true;
	}

	bool MetalShaderProgram::AttachShaderFromString(ShaderStage stage, StringView string)
	{
		static_cast<void>(stage);
		static_cast<void>(string);
		return true;
	}

	bool MetalShaderProgram::AttachShaderFromStrings(ShaderStage stage, ArrayView<const StringView> strings)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		return true;
	}

	bool MetalShaderProgram::AttachShaderFromStringsAndFile(ShaderStage stage, ArrayView<const StringView> strings, StringView filename)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		static_cast<void>(filename);
		return true;
	}

	bool MetalShaderProgram::Link(Introspection introspection)
	{
		return FinalizeAfterLinking(introspection);
	}

	bool MetalShaderProgram::FinalizeAfterLinking(Introspection introspection)
	{
		_introspection = introspection;
		PerformIntrospection();
		return true;
	}

	void MetalShaderProgram::Use()
	{
		MetalDevice::BindProgram(this);
	}

	void MetalShaderProgram::PerformIntrospection()
	{
		if (_introspection != Introspection::Disabled && _status != Status::LinkedWithIntrospection) {
			_uniformsSize = 0;
			_uniformBlocksSize = 0;

			if (_reflection != nullptr) {
				ImportReflection();
				// Compile the MSL, derive the argument table and gather metadata while the reflection (and its
				// embedded sources) is still available.
				BuildPipelineState();
			}
			_status = Status::LinkedWithIntrospection;
		}
		// The reflection is consumed by introspection (its layout is imported into the members above)
		_reflection = nullptr;
	}

	void MetalShaderProgram::BuildPipelineState()
	{
		const ShaderCompiler::ProgramVariant& reflection = *_reflection;
		MTL::Device* device = MtlDevice();
		if (device == nullptr) {
			LOGE("Cannot build Metal pipeline state: device is not created yet");
			return;
		}
		if (reflection.MslVsSource == nullptr || reflection.MslFsSource == nullptr) {
			// No MSL (a construct outside the emitter's subset, or a runtime-compiled shader); draws are skipped
			if (_shouldLogOnErrors) {
				LOGW("Program {} has no MSL sources; draws with it are skipped", _handle);
			}
			return;
		}

		// -- Stage functions: compiled from the embedded MSL by the driver's compiler service, which caches
		// per source text across runs, so only the first launch after a shader change pays the full cost --
		MTL::Library* vsLibrary = nullptr;
		MTL::Function* vsFunction = nullptr;
		MTL::Library* fsLibrary = nullptr;
		MTL::Function* fsFunction = nullptr;
		if (!CompileStage(device, reflection.MslVsSource, "VSMain", _handle, vsLibrary, vsFunction) ||
			!CompileStage(device, reflection.MslFsSource, "FSMain", _handle, fsLibrary, fsFunction)) {
			if (vsFunction != nullptr) vsFunction->release();
			if (vsLibrary != nullptr) vsLibrary->release();
			_status = Status::CompilationFailed;
			return;
		}
		_vsLibrary = vsLibrary;
		_vsFunction = vsFunction;
		_fsLibrary = fsLibrary;
		_fsFunction = fsFunction;

		// -- Argument table, in the offline emitter's scheme: the optional _Globals buffer at index 0, then the
		// std140 blocks at uboBase+i, then the texture/sampler pairs at index j. --
		const bool hasGlobals = (reflection.UniformCount > 0);
		const std::uint32_t uboBase = (hasGlobals ? 1u : 0u);
		_descriptorBindings.clear();
		_looseUniforms.clear();
		_globalsSize = 0;

		if (hasGlobals) {
			// Gather the loose uniforms into the std140 _Globals layout (matches the offline emitter)
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
				_looseUniforms.push_back({ String(u.Name), offset, size });
				offset += size;
			}
			_globalsSize = RoundUp(offset, 16);
			_descriptorBindings.push_back({ DescriptorBinding::Kind::Globals, 0, -1 });
		}

		for (std::size_t i = 0; i < reflection.BlockCount; i++) {
			// The device records block i's bound range in _boundUniformRanges[i] (see MetalShaderUniformBlocks)
			_descriptorBindings.push_back({ DescriptorBinding::Kind::Block, uboBase + std::uint32_t(i), std::int32_t(i) });
		}

		for (std::size_t j = 0; j < reflection.TextureCount; j++) {
			const std::int32_t unit = (reflection.Textures[j].Unit >= 0 ? reflection.Textures[j].Unit : std::int32_t(j));
			_descriptorBindings.push_back({ DescriptorBinding::Kind::Sampler, std::uint32_t(j), unit });
		}
	}

	std::uint32_t MetalShaderProgram::GetVertexInput(VertexAttrib* outAttribs, std::uint32_t maxAttribs, std::uint32_t& outStride, std::uint32_t& outBaseOffset) const
	{
		std::uint32_t count = 0;
		outStride = 0;
		outBaseOffset = 0;
		for (const MetalAttribute& a : _attributes) {
			const std::int32_t loc = a.GetLocation();
			if (loc < 0) {
				continue;
			}
			const MetalVertexFormat::Attribute& fa = _vertexFormat[std::uint32_t(loc)];
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
				const ShaderCompiler::UniformType base = a.GetType();
				attr.ShaderScalar = (base == ShaderCompiler::UniformType::Int || base == ShaderCompiler::UniformType::IVec2 ||
						base == ShaderCompiler::UniformType::IVec3 || base == ShaderCompiler::UniformType::IVec4) ? 1
					: ((base == ShaderCompiler::UniformType::UInt || base == ShaderCompiler::UniformType::UVec2 ||
						base == ShaderCompiler::UniformType::UVec3 || base == ShaderCompiler::UniformType::UVec4) ? 2 : 0);
			}
			if (fa.GetStride() > 0) {
				outStride = std::uint32_t(fa.GetStride());
			}
			outBaseOffset = fa.GetBaseOffset();
		}
		return count;
	}

	void MetalShaderProgram::ImportReflection()
	{
		const ShaderCompiler::ProgramVariant& reflection = *_reflection;
		std::int32_t nextLocation = 0;

		// Loose uniforms - samplers are kept in a separate reflection list but treated as loose uniforms here
		for (std::size_t i = 0; i < reflection.UniformCount; i++) {
			const ShaderCompiler::Uniform& u = reflection.Uniforms[i];
			_uniforms.emplace_back(this, u.Name, u.Type, std::int32_t(u.ArraySize), nextLocation++);
			_uniformsSize += _uniforms.back().GetMemorySize();
		}
		for (std::size_t i = 0; i < reflection.TextureCount; i++) {
			const ShaderCompiler::TextureBinding& t = reflection.Textures[i];
			_uniforms.emplace_back(this, t.Name, ShaderCompiler::UniformType::Sampler2D, 1, nextLocation++);
			_uniformsSize += _uniforms.back().GetMemorySize();
		}

		_uniformBlocks.reserve(reflection.BlockCount);
		for (std::size_t i = 0; i < reflection.BlockCount; i++) {
			const ShaderCompiler::UniformBlock& b = reflection.Blocks[i];

			// A BATCH_SIZE-sized instance array uses the explicitly set batch size, or the same 64 KB-based
			// fallback the in-shader "#ifndef BATCH_SIZE" defaults assume when no size is injected
			std::uint32_t effectiveBatchSize = 0;
			std::uint32_t dataSize = b.BaseSize;
			if (b.InstanceStride > 0) {
				effectiveBatchSize = (_batchSize != std::uint32_t(DefaultBatchSize) && _batchSize > 0)
					? _batchSize : (64u * 1024u) / b.InstanceStride;
				dataSize += b.InstanceStride * effectiveBatchSize;
			}

			const std::uint32_t blockIndex = std::uint32_t(_uniformBlocks.size());
			_uniformBlocks.emplace_back(blockIndex, b.Name, std::int32_t(dataSize));
			MetalUniformBlock& block = _uniformBlocks.back();
			_uniformBlocksSize += block.GetSize();

			if (_introspection != Introspection::NoUniformsInBlocks) {
				block._members.reserve(b.MemberCount);
				for (std::size_t j = 0; j < b.MemberCount; j++) {
					const ShaderCompiler::BlockMember& m = b.Members[j];
					if (m.Type == ShaderCompiler::UniformType::Struct) {
						// Struct aggregates are never read by name; only flat leaf members matter
						continue;
					}
					MetalUniform member;
					member.SetName(m.Name);
					member._type = m.Type;
					member._size = (m.ArraySize == ShaderCompiler::SymbolicArraySize)
						? std::int32_t(effectiveBatchSize)
						: (m.ArraySize > 0 ? std::int32_t(m.ArraySize) : 1);
					member._blockIndex = std::int32_t(blockIndex);
					member._offset = std::int32_t(m.Offset);
					member._owner = this;
					block._members.push_back(member);
				}
			}
		}

		for (std::size_t i = 0; i < reflection.AttributeCount; i++) {
			const ShaderCompiler::Attribute& a = reflection.Attributes[i];
			const std::int32_t location = (a.Location >= 0 ? a.Location : std::int32_t(i));
			_attributes.emplace_back(a.Name, a.Type, location);
			_vertexFormat[std::uint32_t(location)].Init(std::uint32_t(location), std::int32_t(UniformTypeInfo::ComponentCount(a.Type)), 0);
		}
	}

	bool MetalShaderProgram::HasAttribute(const char* name) const
	{
		for (const MetalAttribute& a : _attributes) {
			if (std::strcmp(a.GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	MetalVertexFormat::Attribute* MetalShaderProgram::GetAttribute(const char* name)
	{
		for (const MetalAttribute& a : _attributes) {
			if (std::strcmp(a.GetName(), name) == 0 && a.GetLocation() >= 0) {
				return &_vertexFormat[std::uint32_t(a.GetLocation())];
			}
		}
		return nullptr;
	}

	void MetalShaderProgram::DefineVertexFormat(const MetalBufferObject* vbo, const MetalBufferObject* ibo, std::uint32_t vboOffset)
	{
		_boundVbo = vbo;
		_boundIbo = ibo;
		if (vbo != nullptr) {
			for (const MetalAttribute& a : _attributes) {
				if (a.GetLocation() >= 0) {
					MetalVertexFormat::Attribute& attr = _vertexFormat[std::uint32_t(a.GetLocation())];
					attr.SetVbo(vbo);
					attr.SetBaseOffset(vboOffset);
				}
			}
			_vertexFormat.SetIbo(ibo);
		}
	}

	void MetalShaderProgram::Reset()
	{
		_uniforms.clear();
		_uniformBlocks.clear();
		_attributes.clear();
		_resolvedUniforms.clear();
		_vertexFormat.Reset();

		MetalDevice::OnShaderProgramDestroyed(this);
		ReleaseGpu();
		_descriptorBindings.clear();
		_looseUniforms.clear();
		_globalsSize = 0;

		_status = Status::NotLinked;
		_batchSize = DefaultBatchSize;
		_reflection = nullptr;
	}

	void MetalShaderProgram::SetObjectLabel(StringView label)
	{
		// The backend selects the MSL variant from the reflection, not the label, so this is informational only
		static_cast<void>(label);
	}

	void MetalShaderProgram::SetResolvedUniform(const char* name, const std::uint8_t* data)
	{
		for (ResolvedUniform& r : _resolvedUniforms) {
			if (r.Name == name) {
				r.Data = data;
				return;
			}
		}
		_resolvedUniforms.push_back({name, data});
	}

	const std::uint8_t* MetalShaderProgram::ResolveUniform(const char* name) const
	{
		for (const ResolvedUniform& r : _resolvedUniforms) {
			if (r.Name == name) {
				return r.Data;
			}
		}
		return nullptr;
	}
}
