#include "SwShaderProgram.h"
#include "SwDevice.h"
#include "../../RenderResources.h"

#include <cstring>

namespace nCine::RhiSoftware
{
	namespace
	{
		bool Contains(StringView haystack, const char* needle)
		{
			return haystack.contains(needle);
		}

		SwEffect ClassifyEffect(StringView label)
		{
			// The effect is derived from the program's object label (the shader name registered by
			// ContentResolver::CompileShader, which bakes the variant into the name, e.g. "...Dither").
			// Labels with no matching C++ effect fall through to a logged, skipped draw.

			// Palette family: recolors an R8/RG8 index sprite through the shared palette texture. Checked
			// before the sprite family - "BatchedPaletteRemap" contains "Batched" but not "Sprite".
			if (Contains(label, "PaletteRemap")) {
				return Contains(label, "Batched") ? SwEffect::BatchedPaletteRemap : SwEffect::PaletteRemap;
			}
			// Animated background (planar tunnel and its circular variant)
			if (Contains(label, "TexturedBackground")) {
				return Contains(label, "Circle") ? SwEffect::TexturedBackgroundCircle : SwEffect::TexturedBackground;
			}
			// Viewport compositor (the plain and water variants share the base composite in C++)
			if (Contains(label, "Combine")) {
				return SwEffect::Combine;
			}

			// Default textured sprite family
			const bool isSprite = Contains(label, "Sprite") && !Contains(label, "Mesh") && !Contains(label, "NoTexture");
			if (isSprite && Contains(label, "Batched")) {
				return SwEffect::DefaultBatchedSprites;
			}
			if (isSprite && !Contains(label, "Batched")) {
				return SwEffect::DefaultSprite;
			}
			return SwEffect::Unknown;
		}
	}

	std::uint32_t SwShaderProgram::nextHandle_ = 1;

	SwShaderProgram::SwShaderProgram()
		: SwShaderProgram(QueryPhase::Immediate)
	{
	}

	SwShaderProgram::SwShaderProgram(QueryPhase queryPhase)
		: handle_(nextHandle_++), status_(Status::NotLinked), introspection_(Introspection::Disabled), queryPhase_(queryPhase),
			batchSize_(DefaultBatchSize), shouldLogOnErrors_(true), uniformsSize_(0), uniformBlocksSize_(0),
			reflection_(nullptr), effectReflection_(nullptr), effect_(SwEffect::Unknown), ditherVariant_(false),
			boundVbo_(nullptr), boundIbo_(nullptr)
	{
	}

	SwShaderProgram::SwShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase)
		: SwShaderProgram(queryPhase)
	{
		static_cast<void>(vertexFile);
		static_cast<void>(fragmentFile);
		static_cast<void>(introspection);
	}

	SwShaderProgram::SwShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection)
		: SwShaderProgram(vertexFile, fragmentFile, introspection, QueryPhase::Immediate)
	{
	}

	SwShaderProgram::SwShaderProgram(StringView vertexFile, StringView fragmentFile)
		: SwShaderProgram(vertexFile, fragmentFile, Introspection::Enabled, QueryPhase::Immediate)
	{
	}

	SwShaderProgram::~SwShaderProgram()
	{
		// The pipeline keys per-shader camera uniform data on the program pointer; drop this program's
		// entry so RenderResources::Dispose() finds the map empty (mirrors GLShaderProgram's destructor)
		RenderResources::RemoveCameraUniformData(this);
	}

	bool SwShaderProgram::IsLinked() const
	{
		return (status_ == Status::Linked || status_ == Status::LinkedWithDeferredQueries || status_ == Status::LinkedWithIntrospection);
	}

	bool SwShaderProgram::AttachShaderFromFile(std::uint32_t type, StringView filename)
	{
		static_cast<void>(type);
		static_cast<void>(filename);
		return true;
	}

	bool SwShaderProgram::AttachShaderFromString(std::uint32_t type, StringView string)
	{
		static_cast<void>(type);
		static_cast<void>(string);
		return true;
	}

	bool SwShaderProgram::AttachShaderFromStrings(std::uint32_t type, ArrayView<const StringView> strings)
	{
		static_cast<void>(type);
		static_cast<void>(strings);
		return true;
	}

	bool SwShaderProgram::AttachShaderFromStringsAndFile(std::uint32_t type, ArrayView<const StringView> strings, StringView filename)
	{
		static_cast<void>(type);
		static_cast<void>(strings);
		static_cast<void>(filename);
		return true;
	}

	bool SwShaderProgram::Link(Introspection introspection)
	{
		return FinalizeAfterLinking(introspection);
	}

	bool SwShaderProgram::FinalizeAfterLinking(Introspection introspection)
	{
		introspection_ = introspection;
		PerformIntrospection();
		return true;
	}

	void SwShaderProgram::Use()
	{
		SwDevice::BindProgram(this);
	}

	void SwShaderProgram::PerformIntrospection()
	{
		if (introspection_ != Introspection::Disabled && status_ != Status::LinkedWithIntrospection) {
			uniformsSize_ = 0;
			uniformBlocksSize_ = 0;

			if (reflection_ != nullptr) {
				effectReflection_ = reflection_;
				ImportReflection();
			}
			status_ = Status::LinkedWithIntrospection;
		}
		// The reflection is consumed by introspection (a copy of its layout is kept in effectReflection_)
		reflection_ = nullptr;
	}

	void SwShaderProgram::ImportReflection()
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
			SwUniformBlock& block = uniformBlocks_.back();
			uniformBlocksSize_ += block.GetSize();

			if (introspection_ != Introspection::NoUniformsInBlocks) {
				block.members_.reserve(b.MemberCount);
				for (std::size_t j = 0; j < b.MemberCount; j++) {
					const ShaderCompiler::BlockMember& m = b.Members[j];
					if (m.Type == ShaderCompiler::UniformType::Struct) {
						// Struct aggregates are never read by name; only flat leaf members matter
						continue;
					}
					SwUniform member;
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

	bool SwShaderProgram::HasAttribute(const char* name) const
	{
		for (const SwAttribute& a : attributes_) {
			if (std::strcmp(a.GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	SwVertexFormat::Attribute* SwShaderProgram::GetAttribute(const char* name)
	{
		for (const SwAttribute& a : attributes_) {
			if (std::strcmp(a.GetName(), name) == 0 && a.GetLocation() >= 0) {
				return &vertexFormat_[std::uint32_t(a.GetLocation())];
			}
		}
		return nullptr;
	}

	void SwShaderProgram::DefineVertexFormat(const SwBuffer* vbo, const SwBuffer* ibo, std::uint32_t vboOffset)
	{
		boundVbo_ = vbo;
		boundIbo_ = ibo;
		if (vbo != nullptr) {
			for (const SwAttribute& a : attributes_) {
				if (a.GetLocation() >= 0) {
					SwVertexFormat::Attribute& attr = vertexFormat_[std::uint32_t(a.GetLocation())];
					attr.setVbo(vbo);
					attr.SetBaseOffset(vboOffset);
				}
			}
			vertexFormat_.SetIbo(ibo);
		}
	}

	void SwShaderProgram::Reset()
	{
		uniforms_.clear();
		uniformBlocks_.clear();
		attributes_.clear();
		resolvedUniforms_.clear();
		vertexFormat_.Reset();
		status_ = Status::NotLinked;
		batchSize_ = DefaultBatchSize;
		reflection_ = nullptr;
		effectReflection_ = nullptr;
	}

	void SwShaderProgram::SetObjectLabel(StringView label)
	{
		effect_ = ClassifyEffect(label);
		// The variant is baked into the shader name (e.g. "TexturedBackgroundDither"), so the dithering
		// path is selected from the label rather than re-deriving it from the reflection's defines
		ditherVariant_ = label.contains("Dither");
	}

	const SwUniformBlock* SwShaderProgram::FindBlock(const char* name) const
	{
		for (const SwUniformBlock& block : uniformBlocks_) {
			if (std::strcmp(block.GetName(), name) == 0) {
				return &block;
			}
		}
		return nullptr;
	}

	void SwShaderProgram::SetResolvedUniform(const char* name, const std::uint8_t* data)
	{
		for (ResolvedUniform& r : resolvedUniforms_) {
			if (r.Name == name) {
				r.Data = data;
				return;
			}
		}
		resolvedUniforms_.push_back({name, data});
	}

	const std::uint8_t* SwShaderProgram::ResolveUniform(const char* name) const
	{
		for (const ResolvedUniform& r : resolvedUniforms_) {
			if (r.Name == name) {
				return r.Data;
			}
		}
		return nullptr;
	}
}
