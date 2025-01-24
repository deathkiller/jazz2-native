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
				GLUniformBlockCache* uniformBlock = material.uniformBlock(blockName);
				if (uniformBlock != nullptr) {
					uniform = uniformBlock->uniform(name);
				}
			} else {
				uniform = material.uniform(name);
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
		setNode(node);
		setShader(shader);
	}

	ShaderState::~ShaderState()
	{
		setNode(nullptr);
		setShader(nullptr);
	}

	bool ShaderState::setNode(DrawableNode* node)
	{
		bool nodeHasChanged = false;

		if (node != node_) {
			if (node_ != nullptr) {
				Material& prevMaterial = node_->renderCommand_.material();
				const Material::ShaderProgramType programType = static_cast<Material::ShaderProgramType>(previousShaderType_);
				prevMaterial.setShaderProgramType(programType);
			}

			if (node != nullptr) {
				Material& material = node->renderCommand_.material();
				previousShaderType_ = std::int32_t(material.shaderProgramType());
			}
			node_ = node;

			if (shader_ != nullptr) {
				setShader(shader_);
			}

			nodeHasChanged = true;
		}

		return nodeHasChanged;
	}

	bool ShaderState::setShader(Shader* shader)
	{
		bool shaderHasChanged = false;

		// Allow shader self-assignment to take into account the case where it loads new data
		if (node_ != nullptr) {
			Material& material = node_->renderCommand_.material();
			if (shader == nullptr) {
				const Material::ShaderProgramType programType = static_cast<Material::ShaderProgramType>(previousShaderType_);
				material.setShaderProgramType(programType);
			} else if (shader->isLinked()) {
				if (material.shaderProgramType() != Material::ShaderProgramType::Custom)
					previousShaderType_ = std::int32_t(material.shaderProgramType());

				material.setShaderProgram(shader->glShaderProgram_.get());
			}

			shader_ = shader;
			node_->shaderHasChanged();
			shaderHasChanged = true;
		}

		return shaderHasChanged;
	}

	/*! \note Use this method when the content of the currently assigned shader changes */
	bool ShaderState::resetShader()
	{
		if (shader_ != nullptr && shader_->isLinked() && node_) {
			Material& material = node_->renderCommand_.material();
			material.setShaderProgram(shader_->glShaderProgram_.get());
			node_->shaderHasChanged();
			return true;
		}
		return false;
	}

	/*! \note Contrary to uniforms, there is no need to set the texture again when you reset a shader or when you set a new one */
	bool ShaderState::setTexture(std::uint32_t unit, const Texture* texture)
	{
		if (node_ == nullptr) {
			return false;
		}

		Material& material = node_->renderCommand_.material();
		const bool result = texture ? material.setTexture(unit, *texture) : material.setTexture(unit, nullptr);

		return result;
	}

	bool ShaderState::setUniformInt(const char* blockName, const char* name, const std::int32_t* vector)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr || vector == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.material(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->setIntVector(vector);
		}

		return result;
	}

	bool ShaderState::setUniformInt(const char* blockName, const char* name, std::int32_t value0)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.material(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->setIntValue(value0);
		}

		return result;
	}

	bool ShaderState::setUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.material(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->setIntValue(value0, value1);
		}

		return result;
	}

	bool ShaderState::setUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1, std::int32_t value2)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.material(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->setIntValue(value0, value1, value2);
		}

		return result;
	}

	bool ShaderState::setUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1, std::int32_t value2, std::int32_t value3)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.material(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->setIntValue(value0, value1, value2, value3);
		}

		return result;
	}

	bool ShaderState::setUniformInt(const char* blockName, const char* name, const Vector2i& vec)
	{
		return setUniformInt(blockName, name, vec.X, vec.Y);
	}

	bool ShaderState::setUniformInt(const char* blockName, const char* name, const Vector3i& vector)
	{
		return setUniformInt(blockName, name, vector.X, vector.Y, vector.Z);
	}

	bool ShaderState::setUniformInt(const char* blockName, const char* name, const Vector4i& vector)
	{
		return setUniformInt(blockName, name, vector.X, vector.Y, vector.Z, vector.W);
	}

	bool ShaderState::setUniformFloat(const char* blockName, const char* name, const float* vector)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr || vector == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.material(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->setFloatVector(vector);
		}

		return result;
	}

	bool ShaderState::setUniformFloat(const char* blockName, const char* name, float value0)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.material(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->setFloatValue(value0);
		}

		return result;
	}

	bool ShaderState::setUniformFloat(const char* blockName, const char* name, float value0, float value1)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.material(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->setFloatValue(value0, value1);
		}

		return result;
	}

	bool ShaderState::setUniformFloat(const char* blockName, const char* name, float value0, float value1, float value2)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.material(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->setFloatValue(value0, value1, value2);
		}

		return result;
	}

	bool ShaderState::setUniformFloat(const char* blockName, const char* name, float value0, float value1, float value2, float value3)
	{
		if (node_ == nullptr || shader_ == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformCache* uniform = retrieveUniform(node_->renderCommand_.material(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->setFloatValue(value0, value1, value2, value3);
		}

		return result;
	}

	bool ShaderState::setUniformFloat(const char* blockName, const char* name, const Vector2f& vector)
	{
		return setUniformFloat(blockName, name, vector.X, vector.Y);
	}

	bool ShaderState::setUniformFloat(const char* blockName, const char* name, const Vector3f& vector)
	{
		return setUniformFloat(blockName, name, vector.X, vector.Y, vector.Z);
	}

	bool ShaderState::setUniformFloat(const char* blockName, const char* name, const Vector4f& vector)
	{
		return setUniformFloat(blockName, name, vector.X, vector.Y, vector.Z, vector.W);
	}

	bool ShaderState::setUniformFloat(const char* blockName, const char* name, const Colorf& color)
	{
		return setUniformFloat(blockName, name, color.R, color.G, color.B, color.A);
	}

	std::uint32_t ShaderState::uniformBlockSize(const char* blockName)
	{
		if (node_ == nullptr || shader_ == nullptr || blockName == nullptr) {
			return 0;
		}

		std::uint32_t size = 0;
		GLUniformBlockCache* uniformBlock = node_->renderCommand_.material().uniformBlock(blockName);
		if (uniformBlock != nullptr) {
			size = static_cast<std::uint32_t>(uniformBlock->size());
		}

		return size;
	}

	bool ShaderState::copyToUniformBlock(const char* blockName, std::uint32_t destIndex, std::uint8_t* src, std::uint32_t numBytes)
	{
		if (node_ == nullptr || shader_ == nullptr || blockName == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformBlockCache* uniformBlock = node_->renderCommand_.material().uniformBlock(blockName);
		if (uniformBlock != nullptr) {
			result = uniformBlock->copyData(destIndex, src, numBytes);
		}

		return result;
	}

	bool ShaderState::copyToUniformBlock(const char* blockName, std::uint8_t* src, std::uint32_t numBytes)
	{
		return copyToUniformBlock(blockName, 0, src, numBytes);
	}

	bool ShaderState::copyToUniformBlock(const char* blockName, std::uint8_t* src)
	{
		if (node_ == nullptr || shader_ == nullptr || blockName == nullptr) {
			return false;
		}

		bool result = false;
		GLUniformBlockCache* uniformBlock = node_->renderCommand_.material().uniformBlock(blockName);
		if (uniformBlock != nullptr) {
			result = uniformBlock->copyData(src);
		}

		return result;
	}
}