#include "Material.h"
#include "RenderResources.h"
#include "GL/GLShaderProgram.h"
#include "GL/GLUniform.h"
#include "GL/GLTexture.h"
#include "Texture.h"

#include <cstddef> // for offsetof()

namespace nCine
{
	Material::Material()
		: Material(nullptr, nullptr)
	{
	}

	Material::Material(GLShaderProgram* program, GLTexture* texture)
		: isBlendingEnabled_(false), sortKeyDirty_(true), usedTextureUnits_(texture != nullptr ? 1 : 0),
			srcBlendingFactor_(GL_SRC_ALPHA), destBlendingFactor_(GL_ONE_MINUS_SRC_ALPHA),
			srcAlphaBlendingFactor_(GL_ONE), destAlphaBlendingFactor_(GL_ONE_MINUS_SRC_ALPHA),
			sortKey_(0), shaderChangeCounter_(0),
			shaderProgramType_(ShaderProgramType::Custom), shaderProgram_(program), uniformsHostBufferSize_(0)
	{
		for (std::uint32_t i = 0; i < GLTexture::MaxTextureUnits; i++) {
			textures_[i] = nullptr;
		}
		textures_[0] = texture;

		if (program != nullptr) {
			SetShaderProgram(program);
		}
	}

	void Material::SetBlendingFactors(GLenum srcBlendingFactor, GLenum destBlendingFactor)
	{
		srcBlendingFactor_ = srcBlendingFactor;
		destBlendingFactor_ = destBlendingFactor;
		sortKeyDirty_ = true;

		// Derive correct "over" factors for the alpha channel from the common color-blend presets, so RGBA render
		// targets accumulate proper coverage even though callers only specify color factors. Drawing semi-transparent
		// content with SRC_ALPHA on the alpha channel would erode the destination alpha (a = src.a*src.a + dst.a*(1-src.a));
		// the alpha source factor must be ONE instead. This is harmless for opaque/RGB render targets, where the
		// alpha channel is unused.
		if (srcBlendingFactor == GL_SRC_ALPHA && destBlendingFactor == GL_ONE_MINUS_SRC_ALPHA) {
			srcAlphaBlendingFactor_ = GL_ONE;
			destAlphaBlendingFactor_ = GL_ONE_MINUS_SRC_ALPHA;
		} else if (srcBlendingFactor == GL_SRC_ALPHA && destBlendingFactor == GL_ONE) {
			// Additive: keep the destination coverage unchanged (the source adds light, it does not cover)
			srcAlphaBlendingFactor_ = GL_ZERO;
			destAlphaBlendingFactor_ = GL_ONE;
		} else {
			srcAlphaBlendingFactor_ = srcBlendingFactor;
			destAlphaBlendingFactor_ = destBlendingFactor;
		}
	}

	void Material::SetBlendingFactors(GLenum srcRgbBlendingFactor, GLenum destRgbBlendingFactor, GLenum srcAlphaBlendingFactor, GLenum destAlphaBlendingFactor)
	{
		srcBlendingFactor_ = srcRgbBlendingFactor;
		destBlendingFactor_ = destRgbBlendingFactor;
		srcAlphaBlendingFactor_ = srcAlphaBlendingFactor;
		destAlphaBlendingFactor_ = destAlphaBlendingFactor;
		sortKeyDirty_ = true;
	}

	bool Material::SetShaderProgramType(ShaderProgramType shaderProgramType)
	{
		GLShaderProgram* shaderProgram = RenderResources::GetShaderProgram(shaderProgramType);
		if (shaderProgram == nullptr || shaderProgram == shaderProgram_) {
			return false;
		}

		SetShaderProgram(shaderProgram);

		// Should be assigned after calling `setShaderProgram()`
		shaderProgramType_ = shaderProgramType;
		return true;
	}

	void Material::SetShaderProgram(GLShaderProgram* program)
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
		GLShaderProgram* shaderProgram = shader->glShaderProgram_.get();
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
			uniformsHostBuffer_ = std::make_unique<GLubyte[]>(uniformsSize);
			uniformsHostBufferSize_ = uniformsSize;
		}
		GLubyte* dataPointer = uniformsHostBuffer_.get();
		shaderUniforms_.SetUniformsDataPointer(dataPointer);
		shaderUniformBlocks_.SetUniformsDataPointer(&dataPointer[shaderProgram_->GetUniformsSize()]);
	}

	void Material::SetUniformsDataPointer(GLubyte* dataPointer)
	{
		DEATH_ASSERT(shaderProgram_);
		DEATH_ASSERT(dataPointer);

		uniformsHostBuffer_.reset(nullptr);
		uniformsHostBufferSize_ = 0;
		shaderUniforms_.SetUniformsDataPointer(dataPointer);
		shaderUniformBlocks_.SetUniformsDataPointer(&dataPointer[shaderProgram_->GetUniformsSize()]);
	}

	const GLTexture* Material::GetTexture(std::uint32_t unit) const
	{
		const GLTexture* texture = nullptr;
		if (unit < GLTexture::MaxTextureUnits) {
			texture = textures_[unit];
		}
		return texture;
	}

	bool Material::SetTexture(std::uint32_t unit, const GLTexture* texture)
	{
		bool result = false;
		if (unit < GLTexture::MaxTextureUnits) {
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
		if (unit < GLTexture::MaxTextureUnits) {
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
				GLTexture::Unbind(i);
			}
		}

		if (shaderProgram_) {
			shaderProgram_->Use();
			shaderUniformBlocks_.Bind();
		}
	}

	void Material::DefineVertexFormat(const GLBufferObject* vbo, const GLBufferObject* ibo, std::uint32_t vboOffset)
	{
		shaderProgram_->DefineVertexFormat(vbo, ibo, vboOffset);
	}

	namespace
	{
		uint8_t glBlendingFactorToInt(GLenum blendingFactor)
		{
			switch (blendingFactor) {
				case GL_ZERO: return 0;
				case GL_ONE: return 1;
				case GL_SRC_COLOR: return 2;
				case GL_ONE_MINUS_SRC_COLOR: return 3;
				case GL_DST_COLOR: return 4;
				case GL_ONE_MINUS_DST_COLOR: return 5;
				case GL_SRC_ALPHA: return 6;
				case GL_ONE_MINUS_SRC_ALPHA: return 7;
				case GL_DST_ALPHA: return 8;
				case GL_ONE_MINUS_DST_ALPHA: return 9;
				case GL_CONSTANT_COLOR: return 10;
				case GL_ONE_MINUS_CONSTANT_COLOR: return 11;
				case GL_CONSTANT_ALPHA: return 12;
				case GL_ONE_MINUS_CONSTANT_ALPHA: return 13;
				case GL_SRC_ALPHA_SATURATE: return 14;
			}
			return 0;
		}

		struct SortHashData
		{
			GLuint textures[GLTexture::MaxTextureUnits];
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

		for (std::uint32_t i = 0; i < GLTexture::MaxTextureUnits; i++) {
			hashData.textures[i] = (textures_[i] != nullptr) ? textures_[i]->GetGLHandle() : 0;
		}
		hashData.shaderProgram = shaderProgram_->GetGLHandle();
		hashData.srcBlendingFactor = glBlendingFactorToInt(srcBlendingFactor_);
		hashData.destBlendingFactor = glBlendingFactorToInt(destBlendingFactor_);
		hashData.srcAlphaBlendingFactor = glBlendingFactorToInt(srcAlphaBlendingFactor_);
		hashData.destAlphaBlendingFactor = glBlendingFactorToInt(destAlphaBlendingFactor_);

		sortKey_ = (std::uint32_t)xxHash3(reinterpret_cast<const void*>(&hashData), sizeof(SortHashData), Seed);
		sortKeyDirty_ = false;
		return sortKey_;
	}
}
