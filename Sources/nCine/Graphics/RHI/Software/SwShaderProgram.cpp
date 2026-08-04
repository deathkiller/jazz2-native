#include "SwShaderProgram.h"
#include "SwDevice.h"
#include "../../RenderResources.h"

#include <cstring>

#include <Asserts.h>

namespace nCine::RHI::Software
{
	namespace
	{
		/** @brief One row of the exact-name effect table: a shader label and everything derived from it */
		struct EffectMapping
		{
			const char* Label;
			SwEffect Effect;
			// The dithering variant is baked into the shader name ("...Dither" is a separate program),
			// so the flag travels with the table entry instead of being re-derived by substring
			bool Dither;
		};

		// Every shader label with a hand-written software effect, mapped by exact name. The label set is
		// closed: ContentResolver::CompileShader() bakes each variant into the name and RenderResources
		// registers the built-in defaults, so nothing else reaches SetObjectLabel(). Substring matching
		// made every shader rename a silent behavior change; a label absent from this table stays
		// Unknown and takes the offline-transpiled generated-fragment path (or a logged, skipped draw).
		constexpr EffectMapping EffectMappings[] = {
			// Default sprite programs registered by RenderResources; the mesh variants deliberately keep
			// the generated-fragment path
			{ "Sprite", SwEffect::DefaultSprite, false },
			{ "Sprite_NoTexture", SwEffect::DefaultSpriteNoTexture, false },
			{ "Batched_Sprites", SwEffect::DefaultBatchedSprites, false },
			{ "Batched_Sprites_NoTexture", SwEffect::DefaultBatchedSpritesNoTexture, false },

			// Precompiled shaders registered by ContentResolver::CompileShaders(). The plain and water
			// compositor variants share the base composite in C++.
			{ "Combine", SwEffect::Combine, false },
			{ "CombineWithWater", SwEffect::Combine, false },
			{ "CombineWithWaterLow", SwEffect::Combine, false },
			{ "TexturedBackground", SwEffect::TexturedBackground, false },
			{ "TexturedBackgroundDither", SwEffect::TexturedBackground, true },
			{ "TexturedBackgroundCircle", SwEffect::TexturedBackgroundCircle, false },
			{ "TexturedBackgroundCircleDither", SwEffect::TexturedBackgroundCircle, true },
			{ "PaletteRemap", SwEffect::PaletteRemap, false },
			{ "BatchedPaletteRemap", SwEffect::BatchedPaletteRemap, false },
		};

		const EffectMapping* FindEffectMapping(const char* label)
		{
			// A linear scan is fine - the lookup runs once per program load, not per draw
			for (const EffectMapping& mapping : EffectMappings) {
				if (std::strcmp(mapping.Label, label) == 0) {
					return &mapping;
				}
			}
			return nullptr;
		}

#if defined(DEATH_DEBUG)
		bool Contains(StringView haystack, const char* needle)
		{
			return haystack.contains(needle);
		}

		// The retired substring classifier, kept only to cross-check the table above: debug builds
		// assert the two agree for every classified label. The parity check ships for one release,
		// then this function is deleted (see Docs/FixedFunctionShaderDesign.md, migration plan phase 1).
		SwEffect ClassifyEffectBySubstrings(StringView label)
		{
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

			// No-texture solid-colour sprite family (labels "Sprite_NoTexture" / "Batched_Sprites_NoTexture").
			// The block omits texRect, so it needs a dedicated fast path (different std140 offsets) rather than
			// the generated fragment. The mesh no-texture variants keep the generated-fragment path.
			if (Contains(label, "Sprite") && Contains(label, "NoTexture") && !Contains(label, "Mesh")) {
				return Contains(label, "Batched") ? SwEffect::DefaultBatchedSpritesNoTexture : SwEffect::DefaultSpriteNoTexture;
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
#endif
	}

	std::uint32_t SwShaderProgram::_nextHandle = 1;

	SwShaderProgram::SwShaderProgram()
		: SwShaderProgram(QueryPhase::Immediate)
	{
	}

	SwShaderProgram::SwShaderProgram(QueryPhase queryPhase)
		: _handle(_nextHandle++), _status(Status::NotLinked), _introspection(Introspection::Disabled), _queryPhase(queryPhase),
			_batchSize(DefaultBatchSize), _shouldLogOnErrors(true), _uniformsSize(0), _uniformBlocksSize(0),
			_reflection(nullptr), _effectReflection(nullptr), _effect(SwEffect::Unknown), _ditherVariant(false),
			_boundVbo(nullptr), _boundIbo(nullptr)
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
		return (_status == Status::Linked || _status == Status::LinkedWithDeferredQueries || _status == Status::LinkedWithIntrospection);
	}

	bool SwShaderProgram::AttachShaderFromFile(ShaderStage stage, StringView filename)
	{
		static_cast<void>(stage);
		static_cast<void>(filename);
		return true;
	}

	bool SwShaderProgram::AttachShaderFromString(ShaderStage stage, StringView string)
	{
		static_cast<void>(stage);
		static_cast<void>(string);
		return true;
	}

	bool SwShaderProgram::AttachShaderFromStrings(ShaderStage stage, ArrayView<const StringView> strings)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		return true;
	}

	bool SwShaderProgram::AttachShaderFromStringsAndFile(ShaderStage stage, ArrayView<const StringView> strings, StringView filename)
	{
		static_cast<void>(stage);
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
		_introspection = introspection;
		PerformIntrospection();
		return true;
	}

	void SwShaderProgram::Use()
	{
		SwDevice::BindProgram(this);
	}

	void SwShaderProgram::PerformIntrospection()
	{
		if (_introspection != Introspection::Disabled && _status != Status::LinkedWithIntrospection) {
			_uniformsSize = 0;
			_uniformBlocksSize = 0;

			if (_reflection != nullptr) {
				_effectReflection = _reflection;
				ImportReflection();
			}
			_status = Status::LinkedWithIntrospection;
		}
		// The reflection is consumed by introspection (a copy of its layout is kept in _effectReflection)
		_reflection = nullptr;
	}

	void SwShaderProgram::ImportReflection()
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
			SwUniformBlock& block = _uniformBlocks.back();
			_uniformBlocksSize += block.GetSize();

			if (_introspection != Introspection::NoUniformsInBlocks) {
				block._members.reserve(b.MemberCount);
				for (std::size_t j = 0; j < b.MemberCount; j++) {
					const ShaderCompiler::BlockMember& m = b.Members[j];
					if (m.Type == ShaderCompiler::UniformType::Struct) {
						// Struct aggregates are never read by name; only flat leaf members matter
						continue;
					}
					SwUniform member;
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

	bool SwShaderProgram::HasAttribute(const char* name) const
	{
		for (const SwAttribute& a : _attributes) {
			if (std::strcmp(a.GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	SwVertexFormat::Attribute* SwShaderProgram::GetAttribute(const char* name)
	{
		for (const SwAttribute& a : _attributes) {
			if (std::strcmp(a.GetName(), name) == 0 && a.GetLocation() >= 0) {
				return &_vertexFormat[std::uint32_t(a.GetLocation())];
			}
		}
		return nullptr;
	}

	void SwShaderProgram::DefineVertexFormat(const SwBuffer* vbo, const SwBuffer* ibo, std::uint32_t vboOffset)
	{
		_boundVbo = vbo;
		_boundIbo = ibo;
		if (vbo != nullptr) {
			for (const SwAttribute& a : _attributes) {
				if (a.GetLocation() >= 0) {
					SwVertexFormat::Attribute& attr = _vertexFormat[std::uint32_t(a.GetLocation())];
					attr.setVbo(vbo);
					attr.SetBaseOffset(vboOffset);
				}
			}
			_vertexFormat.SetIbo(ibo);
		}
	}

	void SwShaderProgram::Reset()
	{
		_uniforms.clear();
		_uniformBlocks.clear();
		_attributes.clear();
		_resolvedUniforms.clear();
		_vertexFormat.Reset();
		_status = Status::NotLinked;
		_batchSize = DefaultBatchSize;
		_reflection = nullptr;
		_effectReflection = nullptr;
	}

	void SwShaderProgram::SetObjectLabel(StringView label)
	{
		// The effect is looked up by the program's exact object label (the shader name registered by
		// ContentResolver::CompileShader, which bakes the variant into the name, e.g. "...Dither").
		// Labels with no table entry stay Unknown and take the generated-fragment path.
		_label = label;
		const EffectMapping* mapping = FindEffectMapping(_label.data());
		_effect = (mapping != nullptr ? mapping->Effect : SwEffect::Unknown);
		_ditherVariant = (mapping != nullptr && mapping->Dither);
#if defined(DEATH_DEBUG)
		// Migration parity check (Docs/FixedFunctionShaderDesign.md, phase 1): the table must reproduce
		// the substring classification for every label either side classifies. Ships for one release,
		// then ClassifyEffectBySubstrings() is deleted.
		SwEffect bySubstrings = ClassifyEffectBySubstrings(label);
		if (mapping != nullptr || bySubstrings != SwEffect::Unknown) {
			DEATH_DEBUG_ASSERT(_effect == bySubstrings,
				("Effect table disagrees with the substring classifier for shader label \"{}\"", _label.data()));
			DEATH_DEBUG_ASSERT(_ditherVariant == label.contains("Dither"),
				("Dither flag disagrees with the substring derivation for shader label \"{}\"", _label.data()));
		}
#endif
	}

	const SwUniformBlock* SwShaderProgram::FindBlock(const char* name) const
	{
		for (const SwUniformBlock& block : _uniformBlocks) {
			if (std::strcmp(block.GetName(), name) == 0) {
				return &block;
			}
		}
		return nullptr;
	}

	void SwShaderProgram::SetResolvedUniform(const char* name, const std::uint8_t* data)
	{
		for (ResolvedUniform& r : _resolvedUniforms) {
			if (r.Name == name) {
				r.Data = data;
				return;
			}
		}
		_resolvedUniforms.push_back({name, data});
	}

	const std::uint8_t* SwShaderProgram::ResolveUniform(const char* name) const
	{
		for (const ResolvedUniform& r : _resolvedUniforms) {
			if (r.Name == name) {
				return r.Data;
			}
		}
		return nullptr;
	}
}
