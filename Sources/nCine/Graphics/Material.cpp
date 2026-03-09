#include "Material.h"
#include "RenderResources.h"
#include "Texture.h"

#include <cstddef> // for offsetof()
#include <cstring> // for std::memcpy

namespace nCine
{
	Material::Material()
		: Material(nullptr, nullptr)
	{
	}

	Material::Material(RHI::ShaderProgram* program, RHI::Texture* texture)
		: isBlendingEnabled_(false), srcBlendingFactor_(RHI::BlendFactor::SrcAlpha), destBlendingFactor_(RHI::BlendFactor::OneMinusSrcAlpha),
			shaderProgramType_(ShaderProgramType::Custom), shaderProgram_(program), uniformsHostBufferSize_(0)
	{
		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
			textures_[i] = nullptr;
		}
		textures_[0] = texture;

		if (program != nullptr) {
			SetShaderProgram(program);
		}
	}

	void Material::SetBlendingFactors(RHI::BlendFactor srcBlendingFactor, RHI::BlendFactor destBlendingFactor)
	{
		srcBlendingFactor_ = srcBlendingFactor;
		destBlendingFactor_ = destBlendingFactor;
	}

	bool Material::SetShaderProgramType(ShaderProgramType shaderProgramType)
	{
		RHI::ShaderProgram* shaderProgram = RenderResources::GetShaderProgram(shaderProgramType);
		if (shaderProgram == nullptr || shaderProgram == shaderProgram_) {
			return false;
		}

		SetShaderProgram(shaderProgram);

		// Should be assigned after calling `setShaderProgram()`
		shaderProgramType_ = shaderProgramType;
		return true;
	}

	void Material::SetShaderProgram(RHI::ShaderProgram* program)
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
		RHI::ShaderProgram* shaderProgram = shader->glShaderProgram_.get();
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
		if (shaderProgram_ == nullptr) {
			return; // No shader program — SW backend or uninitialised custom material
		}

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
		if (shaderProgram_ == nullptr || dataPointer == nullptr) {
			return;
		}

		uniformsHostBuffer_.reset(nullptr);
		uniformsHostBufferSize_ = 0;
		shaderUniforms_.SetUniformsDataPointer(dataPointer);
		shaderUniformBlocks_.SetUniformsDataPointer(&dataPointer[shaderProgram_->GetUniformsSize()]);
	}

	void Material::SetSpriteData(const float* texRect, const float* spriteSize, const float* color)
	{
#if defined(RHI_CAP_SHADERS)
		auto* instanceBlock = UniformBlock(InstanceBlockName);
		if (instanceBlock != nullptr) {
			instanceBlock->GetUniform(TexRectUniformName)->SetFloatValue(texRect[0], texRect[1], texRect[2], texRect[3]);
			instanceBlock->GetUniform(SpriteSizeUniformName)->SetFloatVector(spriteSize);
			instanceBlock->GetUniform(ColorUniformName)->SetFloatVector(color);
		}
#else
		RHI::FFState& ff = shaderUniforms_.GetFFState();
		std::memcpy(ff.texRect, texRect, 4 * sizeof(float));
		std::memcpy(ff.spriteSize, spriteSize, 2 * sizeof(float));
		std::memcpy(ff.color, color, 4 * sizeof(float));
		ff.hasTexture = (textures_[0] != nullptr);
		ff.textureUnit = 0;
#endif
	}

	void Material::SetInstColor(const float* rgba4)
	{
#if defined(RHI_CAP_SHADERS)
		auto* instanceBlock = UniformBlock(InstanceBlockName);
		if (instanceBlock != nullptr) {
			instanceBlock->GetUniform(ColorUniformName)->SetFloatVector(rgba4);
		}
#else
		std::memcpy(shaderUniforms_.GetFFState().color, rgba4, 4 * sizeof(float));
#endif
	}

	void Material::SetInstColor(float r, float g, float b, float a)
	{
		const float rgba4[4] = { r, g, b, a };
		SetInstColor(rgba4);
	}

	void Material::SetInstSpriteSize(float w, float h)
	{
#if defined(RHI_CAP_SHADERS)
		auto* instanceBlock = UniformBlock(InstanceBlockName);
		if (instanceBlock != nullptr) {
			instanceBlock->GetUniform(SpriteSizeUniformName)->SetFloatValue(w, h);
		}
#else
		shaderUniforms_.GetFFState().spriteSize[0] = w;
		shaderUniforms_.GetFFState().spriteSize[1] = h;
#endif
	}

	void Material::SetInstTexRect(float sx, float bx, float sy, float by)
	{
#if defined(RHI_CAP_SHADERS)
		auto* instanceBlock = UniformBlock(InstanceBlockName);
		if (instanceBlock != nullptr) {
			instanceBlock->GetUniform(TexRectUniformName)->SetFloatValue(sx, bx, sy, by);
		}
#else
		RHI::FFState& ff = shaderUniforms_.GetFFState();
		ff.texRect[0] = sx;
		ff.texRect[1] = bx;
		ff.texRect[2] = sy;
		ff.texRect[3] = by;
#endif
	}

	void Material::SetModelMatrixUniform(const float* matrix44)
	{
#if defined(RHI_CAP_SHADERS)
		auto* instanceBlock = UniformBlock(InstanceBlockName);
		RHI::UniformCache* matrixUniform = instanceBlock
			? instanceBlock->GetUniform(ModelMatrixUniformName)
			: Uniform(ModelMatrixUniformName);
		if (matrixUniform != nullptr) {
			matrixUniform->SetFloatVector(matrix44);
		}
#else
		std::memcpy(shaderUniforms_.GetFFState().mvpMatrix, matrix44, 16 * sizeof(float));
#endif
	}

	const RHI::Texture* Material::GetTexture(std::uint32_t unit) const
	{
		const RHI::Texture* texture = nullptr;
		if (unit < MaxTextureUnits) {
			texture = textures_[unit];
		}
		return texture;
	}

	bool Material::SetTexture(std::uint32_t unit, const RHI::Texture* texture)
	{
		bool result = false;
		if (unit < MaxTextureUnits) {
			textures_[unit] = texture;
			result = true;
		}
		return result;
	}

	bool Material::SetTexture(std::uint32_t unit, const Texture& texture)
	{
		return SetTexture(unit, texture.texture_.get());
	}

	void Material::Bind()
	{
		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
			if (textures_[i] != nullptr) {
				RHI::BindTexture(*textures_[i], i);
			} else {
				RHI::UnbindTexture(i);
			}
		}

		if (shaderProgram_) {
			shaderProgram_->Use();
			shaderUniformBlocks_.Bind();
		}
	}

	void Material::DefineVertexFormat(const RHI::Buffer* vbo, const RHI::Buffer* ibo, std::uint32_t vboOffset)
	{
		shaderProgram_->DefineVertexFormat(vbo, ibo, vboOffset);
	}

	namespace
	{
		std::uint8_t blendingFactorToInt(RHI::BlendFactor blendingFactor)
		{
			switch (blendingFactor) {
				case RHI::BlendFactor::Zero:                 return 0;
				case RHI::BlendFactor::One:                  return 1;
				case RHI::BlendFactor::SrcColor:             return 2;
				case RHI::BlendFactor::OneMinusSrcColor:     return 3;
				case RHI::BlendFactor::DstColor:             return 4;
				case RHI::BlendFactor::OneMinusDstColor:     return 5;
				case RHI::BlendFactor::SrcAlpha:             return 6;
				case RHI::BlendFactor::OneMinusSrcAlpha:     return 7;
				case RHI::BlendFactor::DstAlpha:             return 8;
				case RHI::BlendFactor::OneMinusDstAlpha:     return 9;
				case RHI::BlendFactor::ConstantColor:        return 10;
				case RHI::BlendFactor::OneMinusConstantColor:return 11;
				case RHI::BlendFactor::ConstantAlpha:        return 12;
				case RHI::BlendFactor::OneMinusConstantAlpha:return 13;
				case RHI::BlendFactor::SrcAlphaSaturate:     return 14;
				default:                                      return 0;
			}
		}

		struct SortHashData
		{
			std::uintptr_t textures[Material::MaxTextureUnits];
			std::uintptr_t shaderProgram;
			std::uint8_t srcBlendingFactor;
			std::uint8_t destBlendingFactor;
		};
	}

	std::uint32_t Material::GetSortKey()
	{
		constexpr std::uint32_t Seed = 1697381921;
		// Align to 64 bits for `fasthash64()` to properly work on Emscripten without alignment faults
		static SortHashData hashData alignas(8);

		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
#if defined(WITH_RHI_GL)
			hashData.textures[i] = (textures_[i] != nullptr) ? textures_[i]->GetGLHandle() : 0;
#else
			hashData.textures[i] = reinterpret_cast<std::uintptr_t>(textures_[i]);
#endif
		}
#if defined(WITH_RHI_GL)
		hashData.shaderProgram = shaderProgram_->GetGLHandle();
#else
		hashData.shaderProgram = reinterpret_cast<std::uintptr_t>(shaderProgram_);
#endif
		hashData.srcBlendingFactor = blendingFactorToInt(srcBlendingFactor_);
		hashData.destBlendingFactor = blendingFactorToInt(destBlendingFactor_);

		return (std::uint32_t)xxHash3(reinterpret_cast<const void*>(&hashData), sizeof(SortHashData), Seed);
	}
}
