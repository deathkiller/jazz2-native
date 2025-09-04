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
		: isBlendingEnabled_(false), srcBlendingFactor_(GL_SRC_ALPHA), destBlendingFactor_(GL_ONE_MINUS_SRC_ALPHA),
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
			result = true;
		}
		return result;
	}

	bool Material::SetTexture(std::uint32_t unit, const Texture& texture)
	{
		return SetTexture(unit, texture.glTexture_.get());
	}

	void Material::Bind()
	{
		for (std::uint32_t i = 0; i < GLTexture::MaxTextureUnits; i++) {
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
		};
	}

	std::uint32_t Material::GetSortKey()
	{
		constexpr std::uint32_t Seed = 1697381921;
		// Align to 64 bits for `fasthash64()` to properly work on Emscripten without alignment faults
		static SortHashData hashData alignas(8);

		for (std::uint32_t i = 0; i < GLTexture::MaxTextureUnits; i++) {
			hashData.textures[i] = (textures_[i] != nullptr) ? textures_[i]->GetGLHandle() : 0;
		}
		hashData.shaderProgram = shaderProgram_->GetGLHandle();
		hashData.srcBlendingFactor = glBlendingFactorToInt(srcBlendingFactor_);
		hashData.destBlendingFactor = glBlendingFactorToInt(destBlendingFactor_);

		return fasthash32(reinterpret_cast<const void*>(&hashData), sizeof(SortHashData), Seed);
	}
}
