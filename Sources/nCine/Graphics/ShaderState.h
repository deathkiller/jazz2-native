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

		inline const DrawableNode* node() const {
			return node_;
		}
		bool setNode(DrawableNode* node);

		inline const Shader* shader() const {
			return shader_;
		}
		bool setShader(Shader* shader);
		/// Triggers a shader update without setting a new shader
		bool resetShader();

		bool setTexture(std::uint32_t unit, const Texture* texture);
		inline bool setTexture(const Texture* texture) {
			return setTexture(0, texture);
		}

		bool setUniformInt(const char* blockName, const char* name, const std::int32_t* vector);
		bool setUniformInt(const char* blockName, const char* name, std::int32_t value0);
		bool setUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1);
		bool setUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1, std::int32_t value2);
		bool setUniformInt(const char* blockName, const char* name, std::int32_t value0, std::int32_t value1, std::int32_t value2, std::int32_t value3);

		bool setUniformInt(const char* blockName, const char* name, const Vector2i& vector);
		bool setUniformInt(const char* blockName, const char* name, const Vector3i& vector);
		bool setUniformInt(const char* blockName, const char* name, const Vector4i& vector);

		bool setUniformFloat(const char* blockName, const char* name, const float* vector);
		bool setUniformFloat(const char* blockName, const char* name, float value0);
		bool setUniformFloat(const char* blockName, const char* name, float value0, float value1);
		bool setUniformFloat(const char* blockName, const char* name, float value0, float value1, float value2);
		bool setUniformFloat(const char* blockName, const char* name, float value0, float value1, float value2, float value3);

		bool setUniformFloat(const char* blockName, const char* name, const Vector2f& vector);
		bool setUniformFloat(const char* blockName, const char* name, const Vector3f& vector);
		bool setUniformFloat(const char* blockName, const char* name, const Vector4f& vector);
		bool setUniformFloat(const char* blockName, const char* name, const Colorf& color);

		std::uint32_t uniformBlockSize(const char* blockName);
		bool copyToUniformBlock(const char* blockName, std::uint32_t destIndex, std::uint8_t* src, std::uint32_t numBytes);
		bool copyToUniformBlock(const char* blockName, std::uint8_t* src, std::uint32_t numBytes);
		bool copyToUniformBlock(const char* blockName, std::uint8_t* src);

	private:
		DrawableNode* node_;
		Shader* shader_;
		std::int32_t previousShaderType_;
	};

}