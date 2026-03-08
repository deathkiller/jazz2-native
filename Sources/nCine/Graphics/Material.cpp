#include "Material.h"
#include "RenderResources.h"
#include "Texture.h"

#include <cstddef> // for offsetof()

namespace nCine
{
	Material::Material()
		: Material(nullptr, nullptr)
	{
	}

	Material::Material(Rhi::ShaderProgram* program, Rhi::Texture* texture)
		: isBlendingEnabled_(false), srcBlendingFactor_(Rhi::BlendFactor::SrcAlpha), destBlendingFactor_(Rhi::BlendFactor::OneMinusSrcAlpha),
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

	void Material::SetBlendingFactors(Rhi::BlendFactor srcBlendingFactor, Rhi::BlendFactor destBlendingFactor)
	{
		srcBlendingFactor_ = srcBlendingFactor;
		destBlendingFactor_ = destBlendingFactor;
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
		if (unit < MaxTextureUnits) {
			texture = textures_[unit];
		}
		return texture;
	}

	bool Material::SetTexture(std::uint32_t unit, const Rhi::Texture* texture)
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
				Rhi::BindTexture(*textures_[i], i);
			} else {
				Rhi::UnbindTexture(i);
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
		std::uint8_t blendingFactorToInt(Rhi::BlendFactor blendingFactor)
		{
			switch (blendingFactor) {
				case Rhi::BlendFactor::Zero:                 return 0;
				case Rhi::BlendFactor::One:                  return 1;
				case Rhi::BlendFactor::SrcColor:             return 2;
				case Rhi::BlendFactor::OneMinusSrcColor:     return 3;
				case Rhi::BlendFactor::DstColor:             return 4;
				case Rhi::BlendFactor::OneMinusDstColor:     return 5;
				case Rhi::BlendFactor::SrcAlpha:             return 6;
				case Rhi::BlendFactor::OneMinusSrcAlpha:     return 7;
				case Rhi::BlendFactor::DstAlpha:             return 8;
				case Rhi::BlendFactor::OneMinusDstAlpha:     return 9;
				case Rhi::BlendFactor::ConstantColor:        return 10;
				case Rhi::BlendFactor::OneMinusConstantColor:return 11;
				case Rhi::BlendFactor::ConstantAlpha:        return 12;
				case Rhi::BlendFactor::OneMinusConstantAlpha:return 13;
				case Rhi::BlendFactor::SrcAlphaSaturate:     return 14;
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
#if defined(RHI_BACKEND_GL)
			hashData.textures[i] = (textures_[i] != nullptr) ? textures_[i]->GetGLHandle() : 0;
#else
			hashData.textures[i] = reinterpret_cast<std::uintptr_t>(textures_[i]);
#endif
		}
#if defined(RHI_BACKEND_GL)
		hashData.shaderProgram = shaderProgram_->GetGLHandle();
#else
		hashData.shaderProgram = reinterpret_cast<std::uintptr_t>(shaderProgram_);
#endif
		hashData.srcBlendingFactor = blendingFactorToInt(srcBlendingFactor_);
		hashData.destBlendingFactor = blendingFactorToInt(destBlendingFactor_);

		return (std::uint32_t)xxHash3(reinterpret_cast<const void*>(&hashData), sizeof(SortHashData), Seed);
	}
}
