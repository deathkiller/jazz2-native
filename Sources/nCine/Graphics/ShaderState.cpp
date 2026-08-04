#include "ShaderState.h"
#include "Shader.h"
#include "DrawableNode.h"
#include "RenderCommand.h"
#include "Material.h"

namespace nCine
{
	namespace
	{
		RHI::UniformCache* retrieveUniform(Material& material, const char* blockName, const char* name)
		{
			RHI::UniformCache* uniform = nullptr;
			if (blockName != nullptr && blockName[0] != '\0') {
				RHI::UniformBlockCache* uniformBlock = material.UniformBlock(blockName);
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
		: _node(nullptr), _shader(nullptr), _previousShaderType(std::int32_t(Material::ShaderProgramType::Custom))
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

		if (node != _node) {
			if (_node != nullptr) {
				Material& prevMaterial = _node->_renderCommand.GetMaterial();
				const Material::ShaderProgramType programType = static_cast<Material::ShaderProgramType>(_previousShaderType);
				prevMaterial.SetShaderProgramType(programType);
			}

			if (node != nullptr) {
				Material& material = node->_renderCommand.GetMaterial();
				_previousShaderType = std::int32_t(material.GetShaderProgramType());
			}
			_node = node;

			if (_shader != nullptr) {
				SetShader(_shader);
			}

			nodeHasChanged = true;
		}

		return nodeHasChanged;
	}

	bool ShaderState::SetShader(Shader* shader)
	{
		bool shaderHasChanged = false;

		// Allow shader self-assignment to take into account the case where it loads new data
		if (_node != nullptr) {
			Material& material = _node->_renderCommand.GetMaterial();
			if (shader == nullptr) {
				const Material::ShaderProgramType programType = static_cast<Material::ShaderProgramType>(_previousShaderType);
				material.SetShaderProgramType(programType);
			} else if (shader->IsLinked()) {
				if (material.GetShaderProgramType() != Material::ShaderProgramType::Custom)
					_previousShaderType = std::int32_t(material.GetShaderProgramType());

				material.SetShaderProgram(shader->_glShaderProgram.get());
			}

			_shader = shader;
			_node->shaderHasChanged();
			shaderHasChanged = true;
		}

		return shaderHasChanged;
	}

	/** @note Use this method when the content of the currently assigned shader changes */
	bool ShaderState::ResetShader()
	{
		if (_shader != nullptr && _shader->IsLinked() && _node) {
			Material& material = _node->_renderCommand.GetMaterial();
			material.SetShaderProgram(_shader->_glShaderProgram.get());
			_node->shaderHasChanged();
			return true;
		}
		return false;
	}

	/** @note Contrary to uniforms, there is no need to set the texture again when resetting or replacing the shader */
	bool ShaderState::SetTexture(std::uint32_t unit, const Texture* texture)
	{
		if (_node == nullptr) {
			return false;
		}

		Material& material = _node->_renderCommand.GetMaterial();
		const bool result = texture ? material.SetTexture(unit, *texture) : material.SetTexture(unit, nullptr);

		return result;
	}

	bool ShaderState::SetUniformInt(const char* blockName, const char* name, const std::int32_t* vector)
	{
		if (_node == nullptr || _shader == nullptr || name == nullptr || vector == nullptr) {
			return false;
		}

		bool result = false;
		RHI::UniformCache* uniform = retrieveUniform(_node->_renderCommand.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetIntVector(vector);
		}

		return result;
	}

	bool ShaderState::SetUniformInt(const char* blockName, const char* name, std::int32_t value0)
	{
		if (_node == nullptr || _shader == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		RHI::UniformCache* uniform = retrieveUniform(_node->_renderCommand.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetIntValue(value0);
		}

		return result;
	}

	bool ShaderState::SetUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1)
	{
		if (_node == nullptr || _shader == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		RHI::UniformCache* uniform = retrieveUniform(_node->_renderCommand.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetIntValue(value0, value1);
		}

		return result;
	}

	bool ShaderState::SetUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1, std::int32_t value2)
	{
		if (_node == nullptr || _shader == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		RHI::UniformCache* uniform = retrieveUniform(_node->_renderCommand.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetIntValue(value0, value1, value2);
		}

		return result;
	}

	bool ShaderState::SetUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1, std::int32_t value2, std::int32_t value3)
	{
		if (_node == nullptr || _shader == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		RHI::UniformCache* uniform = retrieveUniform(_node->_renderCommand.GetMaterial(), blockName, name);
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
		if (_node == nullptr || _shader == nullptr || name == nullptr || vector == nullptr) {
			return false;
		}

		bool result = false;
		RHI::UniformCache* uniform = retrieveUniform(_node->_renderCommand.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetFloatVector(vector);
		}

		return result;
	}

	bool ShaderState::SetUniformFloat(const char* blockName, const char* name, float value0)
	{
		if (_node == nullptr || _shader == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		RHI::UniformCache* uniform = retrieveUniform(_node->_renderCommand.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetFloatValue(value0);
		}

		return result;
	}

	bool ShaderState::SetUniformFloat(const char* blockName, const char* name, float value0, float value1)
	{
		if (_node == nullptr || _shader == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		RHI::UniformCache* uniform = retrieveUniform(_node->_renderCommand.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetFloatValue(value0, value1);
		}

		return result;
	}

	bool ShaderState::SetUniformFloat(const char* blockName, const char* name, float value0, float value1, float value2)
	{
		if (_node == nullptr || _shader == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		RHI::UniformCache* uniform = retrieveUniform(_node->_renderCommand.GetMaterial(), blockName, name);
		if (uniform != nullptr) {
			result = uniform->SetFloatValue(value0, value1, value2);
		}

		return result;
	}

	bool ShaderState::SetUniformFloat(const char* blockName, const char* name, float value0, float value1, float value2, float value3)
	{
		if (_node == nullptr || _shader == nullptr || name == nullptr) {
			return false;
		}

		bool result = false;
		RHI::UniformCache* uniform = retrieveUniform(_node->_renderCommand.GetMaterial(), blockName, name);
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
		if (_node == nullptr || _shader == nullptr || blockName == nullptr) {
			return 0;
		}

		std::uint32_t size = 0;
		RHI::UniformBlockCache* uniformBlock = _node->_renderCommand.GetMaterial().UniformBlock(blockName);
		if (uniformBlock != nullptr) {
			size = static_cast<std::uint32_t>(uniformBlock->GetSize());
		}

		return size;
	}

	bool ShaderState::CopyToUniformBlock(const char* blockName, std::uint32_t destIndex, std::uint8_t* src, std::uint32_t numBytes)
	{
		if (_node == nullptr || _shader == nullptr || blockName == nullptr) {
			return false;
		}

		bool result = false;
		RHI::UniformBlockCache* uniformBlock = _node->_renderCommand.GetMaterial().UniformBlock(blockName);
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
		if (_node == nullptr || _shader == nullptr || blockName == nullptr) {
			return false;
		}

		bool result = false;
		RHI::UniformBlockCache* uniformBlock = _node->_renderCommand.GetMaterial().UniformBlock(blockName);
		if (uniformBlock != nullptr) {
			result = uniformBlock->CopyData(src);
		}

		return result;
	}
}