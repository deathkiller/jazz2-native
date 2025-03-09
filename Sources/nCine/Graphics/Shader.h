#pragma once

#include "GL/GLShaderProgram.h"
#include "../Base/Object.h"

#include <memory>
#include <string>

namespace nCine
{
	/// Shader
	class Shader : public Object
	{
		friend class ShaderState;
		friend class Material;

	public:
		/** @brief Load mode */
		enum class LoadMode {
			String,
			File
		};

		/** @brief Introspection mode */
		enum class Introspection {
			Enabled,
			NoUniformsInBlocks,
			Disabled
		};

		/** @brief Built-in vertex shaders */
		enum class DefaultVertex {
			SPRITE,
			SPRITE_NOTEXTURE,
			MESHSPRITE,
			MESHSPRITE_NOTEXTURE,
			//TEXTNODE,
			BATCHED_SPRITES,
			BATCHED_SPRITES_NOTEXTURE,
			BATCHED_MESHSPRITES,
			BATCHED_MESHSPRITES_NOTEXTURE,
			//BATCHED_TEXTNODES
		};

		/** @brief Built-in fragment shaders */
		enum class DefaultFragment {
			SPRITE,
			//SPRITE_GRAY,
			SPRITE_NOTEXTURE,
			//TEXTNODE_ALPHA,
			//TEXTNODE_RED,
		};

		Shader();

		Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, const char* vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		Shader(const char* shaderName, LoadMode loadMode, const char* vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		Shader(LoadMode loadMode, const char* vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);

		Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, DefaultVertex vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		Shader(const char* shaderName, LoadMode loadMode, DefaultVertex vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		Shader(LoadMode loadMode, DefaultVertex vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, const char* vertex, DefaultFragment fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		Shader(const char* shaderName, LoadMode loadMode, const char* vertex, DefaultFragment fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		Shader(LoadMode loadMode, const char* vertex, DefaultFragment fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);

		~Shader() override;

		Shader(const Shader&) = delete;
		Shader& operator=(const Shader&) = delete;

		bool loadFromMemory(const char* shaderName, Introspection introspection, const char* vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool loadFromMemory(const char* shaderName, const char* vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromMemory(const char* vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);

		bool loadFromMemory(const char* shaderName, Introspection introspection, DefaultVertex vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool loadFromMemory(const char* shaderName, DefaultVertex vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromMemory(DefaultVertex vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromMemory(const char* shaderName, Introspection introspection, const char* vertex, DefaultFragment fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool loadFromMemory(const char* shaderName, const char* vertex, DefaultFragment fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromMemory(const char* vertex, DefaultFragment fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);

		bool loadFromFile(const char* shaderName, Introspection introspection, StringView vertexPath, StringView fragmentPath, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool loadFromFile(const char* shaderName, StringView vertexPath, StringView fragmentPath, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromFile(StringView vertexPath, StringView fragmentPath, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);

		bool loadFromFile(const char* shaderName, Introspection introspection, DefaultVertex vertex, StringView fragmentPath, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool loadFromFile(const char* shaderName, DefaultVertex vertex, StringView fragmentPath, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromFile(DefaultVertex vertex, StringView fragmentPath, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromFile(const char* shaderName, Introspection introspection, StringView vertexPath, DefaultFragment fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool loadFromFile(const char* shaderName, StringView vertexPath, DefaultFragment fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromFile(StringView vertexPath, DefaultFragment fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);

		bool loadFromCache(const char* shaderName, std::uint64_t shaderVersion, Introspection introspection);

		bool saveToCache(const char* shaderName, std::uint64_t shaderVersion) const;

		/// Sets the VBO stride and pointer for the specified vertex attribute
		bool setAttribute(const char* name, std::int32_t stride, void* pointer);

		/// Returns `true` if the shader is linked and can therefore be used
		bool isLinked() const;

		/// Returns the length of the information log including the null termination character
		unsigned int retrieveInfoLogLength() const;
		/// Retrieves the information log and copies it in the provided string object
		void retrieveInfoLog(std::string& infoLog) const;

		/// Returns the automatic log on errors flag
		bool logOnErrors() const;
		/// Sets the automatic log on errors flag
		/*! If the flag is true the shader will automatically log compilation and linking errors. */
		void setLogOnErrors(bool shouldLogOnErrors);

		/// Sets the OpenGL object label for the shader program
		void setGLShaderProgramLabel(const char* label);

		/// Registers a shaders to be used for batches of render commands
		void registerBatchedShader(Shader& batchedShader);

		GLShaderProgram* getHandle() {
			return glShaderProgram_.get();
		}

		inline static ObjectType sType() {
			return ObjectType::Shader;
		}

	private:
		/// OpenGL shader program
		std::unique_ptr<GLShaderProgram> glShaderProgram_;

		bool loadDefaultShader(DefaultVertex vertex, int batchSize);
		bool loadDefaultShader(DefaultFragment fragment);
	};
}