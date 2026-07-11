#include "Material.h"
#include "RenderResources.h"
#include "RHI/Rhi.h"
#include "Texture.h"

#include <cstddef> // for offsetof()

namespace nCine
{
	Material::Material()
		: Material(nullptr, nullptr)
	{
	}

	Material::Material(Rhi::ShaderProgram* program, Rhi::Texture* texture)
		: isBlendingEnabled_(false), sortKeyDirty_(true), usedTextureUnits_(texture != nullptr ? 1 : 0),
			srcBlendingFactor_(BlendingFactor::SrcAlpha), destBlendingFactor_(BlendingFactor::OneMinusSrcAlpha),
			srcAlphaBlendingFactor_(BlendingFactor::One), destAlphaBlendingFactor_(BlendingFactor::OneMinusSrcAlpha),
			sortKey_(0), shaderChangeCounter_(0),
			shaderProgramType_(ShaderProgramType::Custom), shaderProgram_(program), uniformsHostBufferSize_(0)
	{
		for (std::uint32_t i = 0; i < Rhi::Texture::MaxTextureUnits; i++) {
			textures_[i] = nullptr;
		}
		textures_[0] = texture;

		if (program != nullptr) {
			SetShaderProgram(program);
		}
	}

	void Material::SetBlendingFactors(BlendingFactor srcBlendingFactor, BlendingFactor destBlendingFactor)
	{
		srcBlendingFactor_ = srcBlendingFactor;
		destBlendingFactor_ = destBlendingFactor;
		sortKeyDirty_ = true;

		// Derive correct "over" factors for the alpha channel from the common color-blend presets, so RGBA render
		// targets accumulate proper coverage even though callers only specify color factors. Drawing semi-transparent
		// content with SrcAlpha on the alpha channel would erode the destination alpha (a = src.a*src.a + dst.a*(1-src.a));
		// the alpha source factor must be One instead. This is harmless for opaque/RGB render targets, where the
		// alpha channel is unused.
		if (srcBlendingFactor == BlendingFactor::SrcAlpha && destBlendingFactor == BlendingFactor::OneMinusSrcAlpha) {
			srcAlphaBlendingFactor_ = BlendingFactor::One;
			destAlphaBlendingFactor_ = BlendingFactor::OneMinusSrcAlpha;
		} else if (srcBlendingFactor == BlendingFactor::SrcAlpha && destBlendingFactor == BlendingFactor::One) {
			// Additive: keep the destination coverage unchanged (the source adds light, it does not cover)
			srcAlphaBlendingFactor_ = BlendingFactor::Zero;
			destAlphaBlendingFactor_ = BlendingFactor::One;
		} else {
			srcAlphaBlendingFactor_ = srcBlendingFactor;
			destAlphaBlendingFactor_ = destBlendingFactor;
		}
	}

	void Material::SetBlendingFactors(BlendingFactor srcRgbBlendingFactor, BlendingFactor destRgbBlendingFactor, BlendingFactor srcAlphaBlendingFactor, BlendingFactor destAlphaBlendingFactor)
	{
		srcBlendingFactor_ = srcRgbBlendingFactor;
		destBlendingFactor_ = destRgbBlendingFactor;
		srcAlphaBlendingFactor_ = srcAlphaBlendingFactor;
		destAlphaBlendingFactor_ = destAlphaBlendingFactor;
		sortKeyDirty_ = true;
	}

	bool Material::SetShaderProgramType(ShaderProgramType shaderProgramType)
	{
		Rhi::ShaderProgram* shaderProgram = RenderResources::GetShaderProgram(shaderProgramType);
		if (shaderProgram == nullptr || shaderProgram == shaderProgram_) {
			return false;
		}

		SetShaderProgram(shaderProgram);

		// Should be assigned after calling `setShaderProgram()`
		shaderProgramType_ = shaderProgramType;
		return true;
	}

	void Material::SetShaderProgram(Rhi::ShaderProgram* program)
	{
		// Allow self-assignment to take into account the case where the shader program loads new shaders

		shaderProgramType_ = ShaderProgramType::Custom;
		shaderProgram_ = program;
		sortKeyDirty_ = true;
		shaderChangeCounter_++;
		// The camera uniforms are handled separately as they have a different update frequency
		shaderUniforms_.SetProgram(shaderProgram_, nullptr, ProjectionViewMatrixExcludeString);
		shaderUniformBlocks_.SetProgram(shaderProgram_);

		RenderResources::SetDefaultAttributesParameters(*shaderProgram_);
	}

	bool Material::SetShader(Shader* shader)
	{
		Rhi::ShaderProgram* shaderProgram = shader->glShaderProgram_.get();
		if (shaderProgram == shaderProgram_) {
			return false;
		}

		SetShaderProgram(shaderProgram);
		return true;
	}

	void Material::SetDefaultAttributesParameters()
	{
		RenderResources::SetDefaultAttributesParameters(*shaderProgram_);
	}

	void Material::ReserveUniformsDataMemory()
	{
		DEATH_ASSERT(shaderProgram_);

		// Total memory size for all uniforms and uniform blocks
		const std::uint32_t uniformsSize = shaderProgram_->GetUniformsSize() + shaderProgram_->GetUniformBlocksSize();
		if (uniformsSize > uniformsHostBufferSize_) {
			uniformsHostBuffer_ = std::make_unique<std::uint8_t[]>(uniformsSize);
			uniformsHostBufferSize_ = uniformsSize;
		}
		std::uint8_t* dataPointer = uniformsHostBuffer_.get();
		shaderUniforms_.SetUniformsDataPointer(dataPointer);
		shaderUniformBlocks_.SetUniformsDataPointer(&dataPointer[shaderProgram_->GetUniformsSize()]);
	}

	void Material::SetUniformsDataPointer(std::uint8_t* dataPointer)
	{
		DEATH_ASSERT(shaderProgram_);
		DEATH_ASSERT(dataPointer);

		uniformsHostBuffer_.reset(nullptr);
		uniformsHostBufferSize_ = 0;
		shaderUniforms_.SetUniformsDataPointer(dataPointer);
		shaderUniformBlocks_.SetUniformsDataPointer(&dataPointer[shaderProgram_->GetUniformsSize()]);
	}

	const Rhi::Texture* Material::GetTexture(std::uint32_t unit) const
	{
		const Rhi::Texture* texture = nullptr;
		if (unit < Rhi::Texture::MaxTextureUnits) {
			texture = textures_[unit];
		}
		return texture;
	}

	bool Material::SetTexture(std::uint32_t unit, const Rhi::Texture* texture)
	{
		bool result = false;
		if (unit < Rhi::Texture::MaxTextureUnits) {
			textures_[unit] = texture;
			sortKeyDirty_ = true;
			UpdateUsedTextureUnits(unit, texture != nullptr);
			result = true;
		}
		return result;
	}

	bool Material::SetTexture(std::uint32_t unit, const Texture& texture)
	{
		return SetTexture(unit, texture.glTexture_.get());
	}

	bool Material::SetTexture(std::uint32_t unit, std::nullptr_t)
	{
		bool result = false;
		if (unit < Rhi::Texture::MaxTextureUnits) {
			textures_[unit] = nullptr;
			sortKeyDirty_ = true;
			UpdateUsedTextureUnits(unit, false);
			result = true;
		}
		return result;
	}

	void Material::UpdateUsedTextureUnits(std::uint32_t unit, bool textureSet)
	{
		if (textureSet) {
			if (unit >= usedTextureUnits_) {
				usedTextureUnits_ = std::uint8_t(unit + 1);
			}
		} else if (unit + 1 == usedTextureUnits_) {
			// The highest used unit was cleared, find the new highest one
			std::uint32_t n = unit;
			while (n > 0 && textures_[n - 1] == nullptr) {
				n--;
			}
			usedTextureUnits_ = std::uint8_t(n);
		}
	}

	void Material::Bind()
	{
		// Units above `usedTextureUnits_` are intentionally left untouched, samplers of this
		// material's shader only reference units that the material binds itself
		for (std::uint32_t i = 0; i < usedTextureUnits_; i++) {
			if (textures_[i] != nullptr) {
				textures_[i]->Bind(i);
			} else {
				Rhi::Texture::Unbind(i);
			}
		}

		if (shaderProgram_) {
			shaderProgram_->Use();
			shaderUniformBlocks_.Bind();
		}
	}

	void Material::DefineVertexFormat(const Rhi::Buffer* vbo, const Rhi::Buffer* ibo, std::uint32_t vboOffset)
	{
		shaderProgram_->DefineVertexFormat(vbo, ibo, vboOffset);
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
			GLuint textures[Rhi::Texture::MaxTextureUnits];
			GLuint shaderProgram;
			std::uint8_t srcBlendingFactor;
			std::uint8_t destBlendingFactor;
			std::uint8_t srcAlphaBlendingFactor;
			std::uint8_t destAlphaBlendingFactor;
		};
	}

	std::uint32_t Material::GetSortKey()
	{
		if (!sortKeyDirty_) {
			return sortKey_;
		}

		constexpr std::uint32_t Seed = 1697381921;
		// Align to 64 bits for `fasthash64()` to properly work on Emscripten without alignment faults
		SortHashData hashData alignas(8);

		for (std::uint32_t i = 0; i < Rhi::Texture::MaxTextureUnits; i++) {
			hashData.textures[i] = (textures_[i] != nullptr) ? textures_[i]->GetGLHandle() : 0;
		}
		hashData.shaderProgram = shaderProgram_->GetGLHandle();
		hashData.srcBlendingFactor = blendingFactorToInt(srcBlendingFactor_);
		hashData.destBlendingFactor = blendingFactorToInt(destBlendingFactor_);
		hashData.srcAlphaBlendingFactor = blendingFactorToInt(srcAlphaBlendingFactor_);
		hashData.destAlphaBlendingFactor = blendingFactorToInt(destAlphaBlendingFactor_);

		sortKey_ = (std::uint32_t)xxHash3(reinterpret_cast<const void*>(&hashData), sizeof(SortHashData), Seed);
		sortKeyDirty_ = false;
		return sortKey_;
	}
}
