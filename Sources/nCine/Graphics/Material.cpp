#include "Material.h"
#include "RenderResources.h"
#include "RHI/Rhi.h"
#include "Texture.h"
#include "../../Main.h"
#include "../../Shaders/Generated/ShaderCompilerTypes.h"

#include <cstddef> // for offsetof()

namespace nCine
{
	Material::Material()
		: Material(nullptr, nullptr)
	{
	}

	Material::Material(RHI::ShaderProgram* program, RHI::Texture* texture)
		: _isBlendingEnabled(false), _hasOpaqueContentHint(false), _sortKeyDirty(true), _usedTextureUnits(texture != nullptr ? 1 : 0),
			_srcBlendingFactor(BlendingFactor::SrcAlpha), _destBlendingFactor(BlendingFactor::OneMinusSrcAlpha),
			_srcAlphaBlendingFactor(BlendingFactor::One), _destAlphaBlendingFactor(BlendingFactor::OneMinusSrcAlpha),
			_sortKey(0), _shaderChangeCounter(0),
			_shaderProgramType(ShaderProgramType::Custom), _shaderProgram(program), _uniformsHostBufferSize(0)
	{
		for (std::uint32_t i = 0; i < RHI::Texture::MaxTextureUnits; i++) {
			_textures[i] = nullptr;
		}
		_textures[0] = texture;

		if (program != nullptr) {
			SetShaderProgram(program);
		}
	}

	void Material::SetBlendingFactors(BlendingFactor srcBlendingFactor, BlendingFactor destBlendingFactor)
	{
		// Derive correct "over" factors for the alpha channel from the common color-blend presets, so RGBA render
		// targets accumulate proper coverage even though callers only specify color factors. Drawing semi-transparent
		// content with SrcAlpha on the alpha channel would erode the destination alpha (a = src.a*src.a + dst.a*(1-src.a));
		// the alpha source factor must be One instead. This is harmless for opaque/RGB render targets, where the
		// alpha channel is unused.
		BlendingFactor srcAlphaBlendingFactor, destAlphaBlendingFactor;
		if (srcBlendingFactor == BlendingFactor::SrcAlpha && destBlendingFactor == BlendingFactor::OneMinusSrcAlpha) {
			srcAlphaBlendingFactor = BlendingFactor::One;
			destAlphaBlendingFactor = BlendingFactor::OneMinusSrcAlpha;
		} else if (srcBlendingFactor == BlendingFactor::SrcAlpha && destBlendingFactor == BlendingFactor::One) {
			// Additive: keep the destination coverage unchanged (the source adds light, it does not cover)
			srcAlphaBlendingFactor = BlendingFactor::Zero;
			destAlphaBlendingFactor = BlendingFactor::One;
		} else {
			srcAlphaBlendingFactor = srcBlendingFactor;
			destAlphaBlendingFactor = destBlendingFactor;
		}
		SetBlendingFactors(srcBlendingFactor, destBlendingFactor, srcAlphaBlendingFactor, destAlphaBlendingFactor);
	}

	void Material::SetBlendingFactors(BlendingFactor srcRgbBlendingFactor, BlendingFactor destRgbBlendingFactor, BlendingFactor srcAlphaBlendingFactor, BlendingFactor destAlphaBlendingFactor)
	{
		// Only an actual change invalidates the sort key. Re-setting the same factors is the norm for pooled
		// commands - a burst of particles or a tile layer sets them identically on every object, every frame -
		// and each invalidation costs a full hash of the material state in GetSortKey().
		if (_srcBlendingFactor == srcRgbBlendingFactor && _destBlendingFactor == destRgbBlendingFactor &&
			_srcAlphaBlendingFactor == srcAlphaBlendingFactor && _destAlphaBlendingFactor == destAlphaBlendingFactor) {
			return;
		}

		_srcBlendingFactor = srcRgbBlendingFactor;
		_destBlendingFactor = destRgbBlendingFactor;
		_srcAlphaBlendingFactor = srcAlphaBlendingFactor;
		_destAlphaBlendingFactor = destAlphaBlendingFactor;
		_sortKeyDirty = true;
	}

	bool Material::SetShaderProgramType(ShaderProgramType shaderProgramType)
	{
		RHI::ShaderProgram* shaderProgram = RenderResources::GetShaderProgram(shaderProgramType);
		if (shaderProgram == nullptr || shaderProgram == _shaderProgram) {
			return false;
		}

		SetShaderProgram(shaderProgram);

		// Should be assigned after calling `setShaderProgram()`
		_shaderProgramType = shaderProgramType;
		return true;
	}

	void Material::SetShaderProgram(RHI::ShaderProgram* program)
	{
		// Allow self-assignment to take into account the case where the shader program loads new shaders

		_shaderProgramType = ShaderProgramType::Custom;
		_shaderProgram = program;
		_sortKeyDirty = true;
		_shaderChangeCounter++;
		// The camera uniforms are handled separately as they have a different update frequency
		_shaderUniforms.SetProgram(_shaderProgram, nullptr, ProjectionViewMatrixExcludeString);
		_shaderUniformBlocks.SetProgram(_shaderProgram);

		RenderResources::SetDefaultAttributesParameters(*_shaderProgram);
	}

	bool Material::SetShader(Shader* shader)
	{
		RHI::ShaderProgram* shaderProgram = shader->_glShaderProgram.get();
		if (shaderProgram == _shaderProgram) {
			return false;
		}

		SetShaderProgram(shaderProgram);

		// The shader's "render_mode" provides the default blending - an explicit
		// SetBlendingFactors() call after assigning the shader still overrides it
		std::uint32_t renderModes = shader->GetRenderModes();
		if (renderModes != 0) {
			if (renderModes & std::uint32_t(ShaderCompiler::RenderMode::BlendAdd)) {
				SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::One);
			} else if (renderModes & std::uint32_t(ShaderCompiler::RenderMode::BlendMul)) {
				SetBlendingFactors(BlendingFactor::DstColor, BlendingFactor::Zero);
			} else if (renderModes & std::uint32_t(ShaderCompiler::RenderMode::BlendPremulAlpha)) {
				SetBlendingFactors(BlendingFactor::One, BlendingFactor::OneMinusSrcAlpha);
			} else if (renderModes & std::uint32_t(ShaderCompiler::RenderMode::BlendSub)) {
				// Subtractive blending needs blend-equation support, which materials don't have yet
				LOGW("Shader render_mode \"blend_sub\" is not supported yet, falling back to \"blend_mix\"");
				SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
			} else if (renderModes & std::uint32_t(ShaderCompiler::RenderMode::BlendMix)) {
				SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
			}
			SetBlendingEnabled(true);
		}
		return true;
	}

	void Material::SetDefaultAttributesParameters()
	{
		RenderResources::SetDefaultAttributesParameters(*_shaderProgram);
	}

	void Material::ReserveUniformsDataMemory()
	{
		DEATH_ASSERT(_shaderProgram);

		// Total memory size for all uniforms and uniform blocks
		const std::uint32_t uniformsSize = _shaderProgram->GetUniformsSize() + _shaderProgram->GetUniformBlocksSize();
		if (uniformsSize > _uniformsHostBufferSize) {
			_uniformsHostBuffer = std::make_unique<std::uint8_t[]>(uniformsSize);
			_uniformsHostBufferSize = uniformsSize;
		}
		std::uint8_t* dataPointer = _uniformsHostBuffer.get();
		_shaderUniforms.SetUniformsDataPointer(dataPointer);
		_shaderUniformBlocks.SetUniformsDataPointer(&dataPointer[_shaderProgram->GetUniformsSize()]);
	}

	void Material::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		DEATH_ASSERT(_shaderProgram);
		DEATH_ASSERT(dataPointer);

		_uniformsHostBuffer.reset(nullptr);
		_uniformsHostBufferSize = 0;
		_shaderUniforms.SetUniformsDataPointer(dataPointer);
		_shaderUniformBlocks.SetUniformsDataPointer(&dataPointer[_shaderProgram->GetUniformsSize()]);
	}

	const RHI::Texture* Material::GetTexture(std::uint32_t unit) const
	{
		const RHI::Texture* texture = nullptr;
		if (unit < RHI::Texture::MaxTextureUnits) {
			texture = _textures[unit];
		}
		return texture;
	}

	bool Material::SetTexture(std::uint32_t unit, const RHI::Texture* texture)
	{
		bool result = false;
		if (unit < RHI::Texture::MaxTextureUnits) {
			// Rebinding the texture already on the unit leaves the sort key valid (see SetBlendingFactors)
			if (_textures[unit] != texture) {
				_textures[unit] = texture;
				_sortKeyDirty = true;
				UpdateUsedTextureUnits(unit, texture != nullptr);
			}
			result = true;
		}
		return result;
	}

	bool Material::SetTexture(std::uint32_t unit, const Texture& texture)
	{
		return SetTexture(unit, texture._rhiTexture.get());
	}

	bool Material::SetTexture(std::uint32_t unit, std::nullptr_t)
	{
		bool result = false;
		if (unit < RHI::Texture::MaxTextureUnits) {
			if (_textures[unit] != nullptr) {
				_textures[unit] = nullptr;
				_sortKeyDirty = true;
				UpdateUsedTextureUnits(unit, false);
			}
			result = true;
		}
		return result;
	}

	void Material::UpdateUsedTextureUnits(std::uint32_t unit, bool textureSet)
	{
		if (textureSet) {
			if (unit >= _usedTextureUnits) {
				_usedTextureUnits = std::uint8_t(unit + 1);
			}
		} else if (unit + 1 == _usedTextureUnits) {
			// The highest used unit was cleared, find the new highest one
			std::uint32_t n = unit;
			while (n > 0 && _textures[n - 1] == nullptr) {
				n--;
			}
			_usedTextureUnits = std::uint8_t(n);
		}
	}

	void Material::Bind()
	{
		// Units above `_usedTextureUnits` are intentionally left untouched, samplers of this
		// material's shader only reference units that the material binds itself
		for (std::uint32_t i = 0; i < _usedTextureUnits; i++) {
			if (_textures[i] != nullptr) {
				_textures[i]->Bind(i);
			} else {
				RHI::Texture::Unbind(i);
			}
		}

		if (_shaderProgram) {
			_shaderProgram->Use();
			_shaderUniformBlocks.Bind();
		}
	}

	void Material::DefineVertexFormat(const RHI::Buffer* vbo, const RHI::Buffer* ibo, std::uint32_t vboOffset)
	{
		_shaderProgram->DefineVertexFormat(vbo, ibo, vboOffset);
	}

	namespace
	{
		uint8_t blendingFactorToInt(BlendingFactor blendingFactor)
		{
			switch (blendingFactor) {
				case BlendingFactor::Zero: return 0;
				case BlendingFactor::One: return 1;
				case BlendingFactor::SrcColor: return 2;
				case BlendingFactor::OneMinusSrcColor: return 3;
				case BlendingFactor::DstColor: return 4;
				case BlendingFactor::OneMinusDstColor: return 5;
				case BlendingFactor::SrcAlpha: return 6;
				case BlendingFactor::OneMinusSrcAlpha: return 7;
				case BlendingFactor::DstAlpha: return 8;
				case BlendingFactor::OneMinusDstAlpha: return 9;
				case BlendingFactor::ConstantColor: return 10;
				case BlendingFactor::OneMinusConstantColor: return 11;
				case BlendingFactor::ConstantAlpha: return 12;
				case BlendingFactor::OneMinusConstantAlpha: return 13;
				case BlendingFactor::SrcAlphaSaturate: return 14;
			}
			return 0;
		}

		struct SortHashData
		{
			std::uint32_t textures[RHI::Texture::MaxTextureUnits];
			std::uint32_t shaderProgram;
			std::uint8_t srcBlendingFactor;
			std::uint8_t destBlendingFactor;
			std::uint8_t srcAlphaBlendingFactor;
			std::uint8_t destAlphaBlendingFactor;
			// A full 32-bit field so the struct stays padding-free (the hash runs over sizeof(SortHashData);
			// a stray padding byte would feed uninitialized memory into it)
			std::uint32_t opaqueContentHint;
		};
	}

	std::uint32_t Material::GetSortKey()
	{
		if (!_sortKeyDirty) {
			return _sortKey;
		}

		constexpr std::uint32_t Seed = 1697381921;
		// Align to 64 bits for `fasthash64()` to properly work on Emscripten without alignment faults
		SortHashData hashData alignas(8);

		for (std::uint32_t i = 0; i < RHI::Texture::MaxTextureUnits; i++) {
			hashData.textures[i] = (_textures[i] != nullptr) ? _textures[i]->GetUniqueId() : 0;
		}
		hashData.shaderProgram = _shaderProgram->GetUniqueId();
		hashData.srcBlendingFactor = blendingFactorToInt(_srcBlendingFactor);
		hashData.destBlendingFactor = blendingFactorToInt(_destBlendingFactor);
		hashData.srcAlphaBlendingFactor = blendingFactorToInt(_srcAlphaBlendingFactor);
		hashData.destAlphaBlendingFactor = blendingFactorToInt(_destAlphaBlendingFactor);
		hashData.opaqueContentHint = (_hasOpaqueContentHint ? 1 : 0);

		_sortKey = (std::uint32_t)xxHash3(reinterpret_cast<const void*>(&hashData), sizeof(SortHashData), Seed);
		_sortKeyDirty = false;
		return _sortKey;
	}
}
