#include "GxShaderProgram.h"
#include "GxDevice.h"
#include "../../RenderResources.h"

#include <cstring>

namespace nCine::RHI::GX
{
	namespace
	{
		bool Contains(StringView haystack, const char* needle)
		{
			return haystack.contains(needle);
		}

		GxEffect ClassifyEffect(StringView label)
		{
			// The effect is derived from the program's object label (the shader name registered by
			// ContentResolver::CompileShader, which bakes the variant into the name, e.g. "...Dither").
			// Labels with no matching C++ effect fall through to a logged, skipped draw.

			// Tile-layer meshes: a whole visible layer as one triangle list. Checked before the palette
			// family - "TileMapMeshPalette" would otherwise have to be excluded from every later test.
			if (Contains(label, "TileMapMesh")) {
				return Contains(label, "Palette") ? GxEffect::TileMapMeshPalette : GxEffect::TileMapMesh;
			}

			// Palette family: recolors an R8/RG8 index sprite through the shared palette texture. Checked
			// before the sprite family - "BatchedPaletteRemap" contains "Batched" but not "Sprite".
			if (Contains(label, "PaletteRemap")) {
				return Contains(label, "Batched") ? GxEffect::BatchedPaletteRemap : GxEffect::PaletteRemap;
			}

			// Actor state effects. Each has a plain and a "...Palette" variant (indexed sprites); both map
			// to the same effect because the palette lookup is driven by UsesPalette() instead. The partial
			// mask is checked first - "PartialWhiteMask" also contains "WhiteMask".
			const bool batched = Contains(label, "Batched");
			if (Contains(label, "PartialWhiteMask")) {
				return batched ? GxEffect::BatchedPartialWhiteMask : GxEffect::PartialWhiteMask;
			}
			if (Contains(label, "WhiteMask")) {
				return batched ? GxEffect::BatchedWhiteMask : GxEffect::WhiteMask;
			}
			if (Contains(label, "FrozenMask")) {
				return batched ? GxEffect::BatchedFrozenMask : GxEffect::FrozenMask;
			}
			if (Contains(label, "Outline")) {
				return batched ? GxEffect::BatchedOutline : GxEffect::Outline;
			}
			if (Contains(label, "ShieldFire")) {
				return batched ? GxEffect::BatchedShieldFire : GxEffect::ShieldFire;
			}
			if (Contains(label, "ShieldLightning")) {
				return batched ? GxEffect::BatchedShieldLightning : GxEffect::ShieldLightning;
			}
			// Colorized text/sprites (grayscale + dye in GLSL): the fixed-function approximation modulates
			// the mostly grayscale textures with an amplified dye colour computed in Dispatch
			if (Contains(label, "Colorized")) {
				return Contains(label, "Batched") ? GxEffect::BatchedColorized : GxEffect::Colorized;
			}
			// Animated background (planar tunnel and its circular variant)
			if (Contains(label, "TexturedBackground")) {
				return Contains(label, "Circle") ? GxEffect::TexturedBackgroundCircle : GxEffect::TexturedBackground;
			}
			// Viewport compositor (the plain and water variants share the base composite in C++)
			if (Contains(label, "Combine")) {
				return GxEffect::Combine;
			}
			// Level transition (a radial wipe in GLSL, a plain fade here)
			if (Contains(label, "Transition")) {
				return GxEffect::Transition;
			}

			// No-texture solid-colour sprite family (labels "Sprite_NoTexture" / "Batched_Sprites_NoTexture").
			// The block omits texRect, so it needs a dedicated fast path (different std140 offsets) rather than
			// the generated fragment. The mesh no-texture variants keep the generated-fragment path.
			if (Contains(label, "Sprite") && Contains(label, "NoTexture") && !Contains(label, "Mesh")) {
				return Contains(label, "Batched") ? GxEffect::DefaultBatchedSpritesNoTexture : GxEffect::DefaultSpriteNoTexture;
			}

			// Default textured sprite family
			const bool isSprite = Contains(label, "Sprite") && !Contains(label, "Mesh") && !Contains(label, "NoTexture");
			if (isSprite && Contains(label, "Batched")) {
				return GxEffect::DefaultBatchedSprites;
			}
			if (isSprite && !Contains(label, "Batched")) {
				return GxEffect::DefaultSprite;
			}
			return GxEffect::Unknown;
		}
	}

	std::uint32_t GxShaderProgram::nextHandle_ = 1;

	GxShaderProgram::GxShaderProgram()
		: GxShaderProgram(QueryPhase::Immediate)
	{
	}

	GxShaderProgram::GxShaderProgram(QueryPhase queryPhase)
		: handle_(nextHandle_++), status_(Status::NotLinked), introspection_(Introspection::Disabled), queryPhase_(queryPhase),
			batchSize_(DefaultBatchSize), shouldLogOnErrors_(true), uniformsSize_(0), uniformBlocksSize_(0),
			reflection_(nullptr), effectReflection_(nullptr), effect_(GxEffect::Unknown), ditherVariant_(false), usesPalette_(false),
			boundVbo_(nullptr), boundVboOffset_(0), boundIbo_(nullptr)
	{
	}

	GxShaderProgram::GxShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase)
		: GxShaderProgram(queryPhase)
	{
		static_cast<void>(vertexFile);
		static_cast<void>(fragmentFile);
		static_cast<void>(introspection);
	}

	GxShaderProgram::GxShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection)
		: GxShaderProgram(vertexFile, fragmentFile, introspection, QueryPhase::Immediate)
	{
	}

	GxShaderProgram::GxShaderProgram(StringView vertexFile, StringView fragmentFile)
		: GxShaderProgram(vertexFile, fragmentFile, Introspection::Enabled, QueryPhase::Immediate)
	{
	}

	GxShaderProgram::~GxShaderProgram()
	{
		// The pipeline keys per-shader camera uniform data on the program pointer; drop this program's
		// entry so RenderResources::Dispose() finds the map empty (mirrors GLShaderProgram's destructor)
		RenderResources::RemoveCameraUniformData(this);
	}

	bool GxShaderProgram::IsLinked() const
	{
		return (status_ == Status::Linked || status_ == Status::LinkedWithDeferredQueries || status_ == Status::LinkedWithIntrospection);
	}

	bool GxShaderProgram::AttachShaderFromFile(ShaderStage stage, StringView filename)
	{
		static_cast<void>(stage);
		static_cast<void>(filename);
		return true;
	}

	bool GxShaderProgram::AttachShaderFromString(ShaderStage stage, StringView string)
	{
		static_cast<void>(stage);
		static_cast<void>(string);
		return true;
	}

	bool GxShaderProgram::AttachShaderFromStrings(ShaderStage stage, ArrayView<const StringView> strings)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		return true;
	}

	bool GxShaderProgram::AttachShaderFromStringsAndFile(ShaderStage stage, ArrayView<const StringView> strings, StringView filename)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		static_cast<void>(filename);
		return true;
	}

	bool GxShaderProgram::Link(Introspection introspection)
	{
		return FinalizeAfterLinking(introspection);
	}

	bool GxShaderProgram::FinalizeAfterLinking(Introspection introspection)
	{
		introspection_ = introspection;
		PerformIntrospection();
		return true;
	}

	void GxShaderProgram::Use()
	{
		GxDevice::BindProgram(this);
	}

	void GxShaderProgram::PerformIntrospection()
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

	void GxShaderProgram::ImportReflection()
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
			GxUniformBlock& block = uniformBlocks_.back();
			uniformBlocksSize_ += block.GetSize();

			if (introspection_ != Introspection::NoUniformsInBlocks) {
				block.members_.reserve(b.MemberCount);
				for (std::size_t j = 0; j < b.MemberCount; j++) {
					const ShaderCompiler::BlockMember& m = b.Members[j];
					if (m.Type == ShaderCompiler::UniformType::Struct) {
						// Struct aggregates are never read by name; only flat leaf members matter
						continue;
					}
					GxUniform member;
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

	bool GxShaderProgram::HasAttribute(const char* name) const
	{
		for (const GxAttribute& a : attributes_) {
			if (std::strcmp(a.GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	GxVertexFormat::Attribute* GxShaderProgram::GetAttribute(const char* name)
	{
		for (const GxAttribute& a : attributes_) {
			if (std::strcmp(a.GetName(), name) == 0 && a.GetLocation() >= 0) {
				return &vertexFormat_[std::uint32_t(a.GetLocation())];
			}
		}
		return nullptr;
	}

	void GxShaderProgram::DefineVertexFormat(const GxBuffer* vbo, const GxBuffer* ibo, std::uint32_t vboOffset)
	{
		boundVbo_ = vbo;
		boundVboOffset_ = vboOffset;
		boundIbo_ = ibo;
		if (vbo != nullptr) {
			for (const GxAttribute& a : attributes_) {
				if (a.GetLocation() >= 0) {
					GxVertexFormat::Attribute& attr = vertexFormat_[std::uint32_t(a.GetLocation())];
					attr.setVbo(vbo);
					attr.SetBaseOffset(vboOffset);
				}
			}
			vertexFormat_.SetIbo(ibo);
		}
	}

	void GxShaderProgram::Reset()
	{
		uniforms_.clear();
		uniformBlocks_.clear();
		attributes_.clear();
		resolvedUniforms_.clear();
		resolvedProjectionMatrix_ = nullptr;
		resolvedViewMatrix_ = nullptr;
		vertexFormat_.Reset();
		status_ = Status::NotLinked;
		batchSize_ = DefaultBatchSize;
		reflection_ = nullptr;
		effectReflection_ = nullptr;
	}

	void GxShaderProgram::SetObjectLabel(StringView label)
	{
		label_ = label;
		effect_ = ClassifyEffect(label);
		// The variant is baked into the shader name (e.g. "TexturedBackgroundDither"), so the dithering
		// path is selected from the label rather than re-deriving it from the reflection's defines
		ditherVariant_ = label.contains("Dither");
		// "PaletteRemap" and the "...Palette" variants of the actor state effects all sample indexed
		// textures through the palette texture bound at unit 1
		usesPalette_ = label.contains("Palette");
	}

	const GxUniformBlock* GxShaderProgram::FindBlock(const char* name) const
	{
		for (const GxUniformBlock& block : uniformBlocks_) {
			if (std::strcmp(block.GetName(), name) == 0) {
				return &block;
			}
		}
		return nullptr;
	}

	void GxShaderProgram::SetResolvedUniform(const char* name, const std::uint8_t* data)
	{
		// The device dispatch reads the camera matrices on every draw; recognizing the two names here,
		// where a value is published at most once per commit, turns that per-draw linear scan with
		// string compares into a plain member read (see GetResolvedProjectionMatrix())
		if (std::strcmp(name, "uProjectionMatrix") == 0) {
			resolvedProjectionMatrix_ = data;
		} else if (std::strcmp(name, "uViewMatrix") == 0) {
			resolvedViewMatrix_ = data;
		}
		for (ResolvedUniform& r : resolvedUniforms_) {
			if (r.Name == name) {
				r.Data = data;
				return;
			}
		}
		resolvedUniforms_.push_back({name, data});
	}

	const std::uint8_t* GxShaderProgram::ResolveUniform(const char* name) const
	{
		for (const ResolvedUniform& r : resolvedUniforms_) {
			if (r.Name == name) {
				return r.Data;
			}
		}
		return nullptr;
	}
}
