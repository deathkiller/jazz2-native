#pragma once

#include "../../Main.h"
#include "../Primitives/Vector4.h"
#include "../Primitives/Colorf.h"

namespace nCine
{
	class DrawableNode;
	class Shader;
	class Texture;

	/// Shader state class for the user to use custom shaders
	class ShaderState
	{
	public:
		ShaderState();
		ShaderState(DrawableNode* node, Shader* shader);
		~ShaderState();

		ShaderState(const ShaderState&) = delete;
		ShaderState& operator=(const ShaderState&) = delete;

		inline const DrawableNode* GetNode() const {
			return node_;
		}
		bool SetNode(DrawableNode* node);

		inline const Shader* GetShader() const {
			return shader_;
		}
		bool SetShader(Shader* shader);
		/// Triggers a shader update without setting a new shader
		bool ResetShader();

		bool SetTexture(std::uint32_t unit, const Texture* texture);
		inline bool SetTexture(const Texture* texture) {
			return SetTexture(0, texture);
		}

		bool SetUniformInt(const char* blockName, const char* name, const std::int32_t* vector);
		bool SetUniformInt(const char* blockName, const char* name, std::int32_t value0);
		bool SetUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1);
		bool SetUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1, std::int32_t value2);
		bool SetUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1, std::int32_t value2, std::int32_t value3);

		bool SetUniformInt(const char* blockName, const char* name, const Vector2i& vector);
		bool SetUniformInt(const char* blockName, const char* name, const Vector3i& vector);
		bool SetUniformInt(const char* blockName, const char* name, const Vector4i& vector);

		bool SetUniformFloat(const char* blockName, const char* name, const float* vector);
		bool SetUniformFloat(const char* blockName, const char* name, float value0);
		bool SetUniformFloat(const char* blockName, const char* name, float value0, float value1);
		bool SetUniformFloat(const char* blockName, const char* name, float value0, float value1, float value2);
		bool SetUniformFloat(const char* blockName, const char* name, float value0, float value1, float value2, float value3);

		bool SetUniformFloat(const char* blockName, const char* name, const Vector2f& vector);
		bool SetUniformFloat(const char* blockName, const char* name, const Vector3f& vector);
		bool SetUniformFloat(const char* blockName, const char* name, const Vector4f& vector);
		bool SetUniformFloat(const char* blockName, const char* name, const Colorf& color);

		std::uint32_t GetUniformBlockSize(const char* blockName);
		bool CopyToUniformBlock(const char* blockName, std::uint32_t destIndex, std::uint8_t* src, std::uint32_t numBytes);
		bool CopyToUniformBlock(const char* blockName, std::uint8_t* src, std::uint32_t numBytes);
		bool CopyToUniformBlock(const char* blockName, std::uint8_t* src);

	private:
		DrawableNode* node_;
		Shader* shader_;
		std::int32_t previousShaderType_;
	};

}