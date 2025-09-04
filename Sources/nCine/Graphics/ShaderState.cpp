#include "ShaderState.h"
#include "Shader.h"
#include "DrawableNode.h"
#include "RenderCommand.h"
#include "Material.h"

namespace nCine
{
	namespace
	{
		GLUniformCache* retrieveUniform(Material& material, const char* blockName, const char* name)
		{
			GLUniformCache* uniform = nullptr;
			if (blockName != nullptr && blockName[0] != '\0') {
				GLUniformBlockCache* uniformBlock = material.UniformBlock(blockName);
				if (uniformBlock != nullptr) {
					uniform = uniformBlock->GetUniform(name);
				}
			} else {
				uniform = material.Uniform(name);
			}
			return uniform;
		}
	}

	ShaderState::ShaderState()
		: ShaderState(nullptr, nullptr)
	{
	}

	ShaderState::ShaderState(DrawableNode* node, Shader* shader)
		: node_(nullptr), shader_(nullptr), previousShaderType_(std::int32_t(Material::ShaderProgramType::Custom))
	{
		SetNode(node);
		SetShader(shader);
	}

	ShaderState::~ShaderState()
	{
		SetNode(nullptr);
		SetShader(nullptr);
	}

	bool ShaderState::SetNode(DrawableNode* node)
	{
		bool nodeHasChanged = false;

		if (node != node_) {
			if (node_ != nullptr) {
				Material& prevMaterial = node_->renderCommand_.GetMaterial();
				const Material::ShaderProgramType programType = static_cast<Material::ShaderProgramType>(previousShaderType_);
				prevMaterial.SetShaderProgramType(programType);
			}

			if (node != nullptr) {
				Material& material = node->renderCommand_.GetMaterial();
				previousShaderType_ = std::int32_t(material.GetShaderProgramType());
			}
			node_ = node;

			if (shader_ != nullptr) {
				SetShader(shader_);
			}

			nodeHasChanged = true;
		}

		return nodeHasChanged;
	}

	bool ShaderState::SetShader(Shader* shader)
	{
		bool shaderHasChanged = false;

		// Allow shader self-assignment to take into account the case where it loads new data
		if (node_ != nullptr) {
			Material& material = node_->renderCommand_.GetMaterial();
			if (shader == nullptr) {
				const Material::ShaderProgramType programType = static_cast<Material::ShaderProgramType>(previousShaderType_);
				material.SetShaderProgramType(programType);
			} else if (shader->IsLinked()) {
				if (material.GetShaderProgramType() != Material::ShaderProgramType::Custom)
					previousShaderType_ = std::int32_t(material.GetShaderProgramType());

				material.SetShaderProgram(shader->glShaderProgram_.get());
			}

			shader_ = shader;
			node_->shaderHasChanged();
			shaderHasChanged = true;
		}

		return shaderHasChanged;
	}

	/*! \note Use this method when the content of the currently assigned shader changes */
	bool ShaderState::ResetShader()
	{
		if (shader_ != nullptr && shader_->IsLinked() && node_) {
			Material& material = node_->renderCommand_.GetMaterial();
			material.SetShaderProgram(shader_->glShaderProgram_.get());
			node_->shaderHasChanged();
			return true;
		}
		return false;
	}

	/*! \note Contrary to uniforms, there is no need to set the texture again when you reset a shader or when you set a new one */
	bool ShaderState::SetTexture(std::uint32_t unit, const Texture* texture)
	{
		if (node_ == nullptr) {
			return false;
		}

		Material& material = node_->renderCommand_.GetMaterial();
		const bool result = texture ? material.SetTexture(unit, *texture) : material.SetTexture(unit, nullptr);

		return result;
	}

	bool ShaderState::SetUniformInt(const char* blockName, const char* name, const std::int32_t* vector)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr || vector == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetIntVector(vector);
		}

		return result;
	}

	bool ShaderState::SetUniformInt(const char* blockName, const char* name, std::int32_t value0)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetIntValue(value0);
		}

		return result;
	}

	bool ShaderState::SetUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetIntValue(value0, value1);
		}

		return result;
	}

	bool ShaderState::SetUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1, std::int32_t value2)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetIntValue(value0, value1, value2);
		}

		return result;
	}

	bool ShaderState::SetUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1, std::int32_t value2, std::int32_t value3)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetIntValue(value0, value1, value2, value3);
		}

		return result;
	}

	bool ShaderState::SetUniformInt(const char* blockName, const char* name, const Vector2i& vec)
	{
		return SetUniformInt(blockName, name, vec.X, vec.Y);
	}

	bool ShaderState::SetUniformInt(const char* blockName, const char* name, const Vector3i& vector)
	{
		return SetUniformInt(blockName, name, vector.X, vector.Y, vector.Z);
	}

	bool ShaderState::SetUniformInt(const char* blockName, const char* name, const Vector4i& vector)
	{
		return SetUniformInt(blockName, name, vector.X, vector.Y, vector.Z, vector.W);
	}

	bool ShaderState::SetUniformFloat(const char* blockName, const char* name, const float* vector)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr || vector == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetFloatVector(vector);
		}

		return result;
	}

	bool ShaderState::SetUniformFloat(const char* blockName, const char* name, float value0)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetFloatValue(value0);
		}

		return result;
	}

	bool ShaderState::SetUniformFloat(const char* blockName, const char* name, float value0, float value1)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetFloatValue(value0, value1);
		}

		return result;
	}

	bool ShaderState::SetUniformFloat(const char* blockName, const char* name, float value0, float value1, float value2)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetFloatValue(value0, value1, value2);
		}

		return result;
	}

	bool ShaderState::SetUniformFloat(const char* blockName, const char* name, float value0, float value1, float value2, float value3)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetFloatValue(value0, value1, value2, value3);
		}

		return result;
	}

	bool ShaderState::SetUniformFloat(const char* blockName, const char* name, const Vector2f& vector)
	{
		return SetUniformFloat(blockName, name, vector.X, vector.Y);
	}

	bool ShaderState::SetUniformFloat(const char* blockName, const char* name, const Vector3f& vector)
	{
		return SetUniformFloat(blockName, name, vector.X, vector.Y, vector.Z);
	}

	bool ShaderState::SetUniformFloat(const char* blockName, const char* name, const Vector4f& vector)
	{
		return SetUniformFloat(blockName, name, vector.X, vector.Y, vector.Z, vector.W);
	}

	bool ShaderState::SetUniformFloat(const char* blockName, const char* name, const Colorf& color)
	{
		return SetUniformFloat(blockName, name, color.R, color.G, color.B, color.A);
	}

	std::uint32_t ShaderState::GetUniformBlockSize(const char* blockName)
	{
		if (node_ == nullptr || shader_ == nullptr || blockName == nullptr) {
			return 0;
		}

		std::uint32_t size = 0;
		GLUniformBlockCache* uniformBlock = node_->renderCommand_.GetMaterial().UniformBlock(blockName);
		if (uniformBlock != nullptr) {
			size = static_cast<std::uint32_t>(uniformBlock->GetSize());
		}

		return size;
	}

	bool ShaderState::CopyToUniformBlock(const char* blockName, std::uint32_t destIndex, std::uint8_t* src, std::uint32_t numBytes)
	{
		if (node_ == nullptr || shader_ == nullptr || blockName == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformBlockCache* uniformBlock = node_->renderCommand_.GetMaterial().UniformBlock(blockName);
		if (uniformBlock != nullptr) {
			result = uniformBlock->CopyData(destIndex, src, numBytes);
		}

		return result;
	}

	bool ShaderState::CopyToUniformBlock(const char* blockName, std::uint8_t* src, std::uint32_t numBytes)
	{
		return CopyToUniformBlock(blockName, 0, src, numBytes);
	}

	bool ShaderState::CopyToUniformBlock(const char* blockName, std::uint8_t* src)
	{
		if (node_ == nullptr || shader_ == nullptr || blockName == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformBlockCache* uniformBlock = node_->renderCommand_.GetMaterial().UniformBlock(blockName);
		if (uniformBlock != nullptr) {
			result = uniformBlock->CopyData(src);
		}

		return result;
	}
}