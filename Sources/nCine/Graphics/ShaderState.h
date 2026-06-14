#pragma once

#include "../../Main.h"
#include "../Primitives/Vector4.h"
#include "../Primitives/Colorf.h"

namespace nCine
{
	class DrawableNode;
	class Shader;
	class Texture;

	/**
		@brief Binds a custom shader to a drawable node and feeds its uniforms and textures
		
		Lets the user attach a @ref Shader to a @ref DrawableNode and set its uniform values,
		uniform block contents and texture units. Returns the node to its default shader when
		assigned a `nullptr` shader.
	*/
	class ShaderState
	{
	public:
		ShaderState();
		ShaderState(DrawableNode* node, Shader* shader);
		~ShaderState();

		ShaderState(const ShaderState&) = delete;
		ShaderState& operator=(const ShaderState&) = delete;

		/** @brief Returns the associated drawable node */
		inline const DrawableNode* GetNode() const {
			return node_;
		}
		/** @brief Sets the associated drawable node */
		bool SetNode(DrawableNode* node);

		/** @brief Returns the assigned shader */
		inline const Shader* GetShader() const {
			return shader_;
		}
		/** @brief Assigns a shader to the node, or restores the default shader when `nullptr` */
		bool SetShader(Shader* shader);
		/**
		 * @brief Triggers a shader update without setting a new shader
		 *
		 * @note Use this method when the content of the currently assigned shader changes.
		 */
		bool ResetShader();

		/** @brief Sets the texture bound to the specified texture unit */
		bool SetTexture(std::uint32_t unit, const Texture* texture);
		/** @brief Sets the texture bound to the first texture unit */
		inline bool SetTexture(const Texture* texture) {
			return SetTexture(0, texture);
		}

		/** @brief Sets an integer uniform value from a vector pointer */
		bool SetUniformInt(const char* blockName, const char* name, const std::int32_t* vector);
		/** @brief Sets an integer uniform from one to four scalar components */
		bool SetUniformInt(const char* blockName, const char* name, std::int32_t value0);
		/** @brief Sets an integer uniform from one to four scalar components */
		bool SetUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1);
		/** @brief Sets an integer uniform from one to four scalar components */
		bool SetUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1, std::int32_t value2);
		/** @brief Sets an integer uniform from one to four scalar components */
		bool SetUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1, std::int32_t value2, std::int32_t value3);

		/** @brief Sets an integer uniform from an integer vector */
		bool SetUniformInt(const char* blockName, const char* name, const Vector2i& vector);
		/** @brief Sets an integer uniform from an integer vector */
		bool SetUniformInt(const char* blockName, const char* name, const Vector3i& vector);
		/** @brief Sets an integer uniform from an integer vector */
		bool SetUniformInt(const char* blockName, const char* name, const Vector4i& vector);

		/** @brief Sets a float uniform value from a vector pointer */
		bool SetUniformFloat(const char* blockName, const char* name, const float* vector);
		/** @brief Sets a float uniform from one to four scalar components */
		bool SetUniformFloat(const char* blockName, const char* name, float value0);
		/** @brief Sets a float uniform from one to four scalar components */
		bool SetUniformFloat(const char* blockName, const char* name, float value0, float value1);
		/** @brief Sets a float uniform from one to four scalar components */
		bool SetUniformFloat(const char* blockName, const char* name, float value0, float value1, float value2);
		/** @brief Sets a float uniform from one to four scalar components */
		bool SetUniformFloat(const char* blockName, const char* name, float value0, float value1, float value2, float value3);

		/** @brief Sets a float uniform from a float vector */
		bool SetUniformFloat(const char* blockName, const char* name, const Vector2f& vector);
		/** @brief Sets a float uniform from a float vector */
		bool SetUniformFloat(const char* blockName, const char* name, const Vector3f& vector);
		/** @brief Sets a float uniform from a float vector */
		bool SetUniformFloat(const char* blockName, const char* name, const Vector4f& vector);
		/** @brief Sets a float uniform from a color */
		bool SetUniformFloat(const char* blockName, const char* name, const Colorf& color);

		/** @brief Returns the size in bytes of the specified uniform block */
		std::uint32_t GetUniformBlockSize(const char* blockName);
		/** @brief Copies raw data into the specified uniform block at the given element index */
		bool CopyToUniformBlock(const char* blockName, std::uint32_t destIndex, std::uint8_t* src, std::uint32_t numBytes);
		/** @brief Copies the specified number of bytes into the uniform block */
		bool CopyToUniformBlock(const char* blockName, std::uint8_t* src, std::uint32_t numBytes);
		/** @brief Copies data filling the whole uniform block */
		bool CopyToUniformBlock(const char* blockName, std::uint8_t* src);

	private:
		DrawableNode* node_;
		Shader* shader_;
		std::int32_t previousShaderType_;
	};

}