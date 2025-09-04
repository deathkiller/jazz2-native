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

		bool LoadFromMemory(const char* shaderName, Introspection introspection, const char* vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool LoadFromMemory(const char* shaderName, const char* vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool LoadFromMemory(const char* vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);

		bool LoadFromMemory(const char* shaderName, Introspection introspection, DefaultVertex vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool LoadFromMemory(const char* shaderName, DefaultVertex vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool LoadFromMemory(DefaultVertex vertex, const char* fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool LoadFromMemory(const char* shaderName, Introspection introspection, const char* vertex, DefaultFragment fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool LoadFromMemory(const char* shaderName, const char* vertex, DefaultFragment fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool LoadFromMemory(const char* vertex, DefaultFragment fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);

		bool LoadFromFile(const char* shaderName, Introspection introspection, StringView vertexPath, StringView fragmentPath, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool LoadFromFile(const char* shaderName, StringView vertexPath, StringView fragmentPath, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool LoadFromFile(StringView vertexPath, StringView fragmentPath, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);

		bool LoadFromFile(const char* shaderName, Introspection introspection, DefaultVertex vertex, StringView fragmentPath, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool LoadFromFile(const char* shaderName, DefaultVertex vertex, StringView fragmentPath, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool LoadFromFile(DefaultVertex vertex, StringView fragmentPath, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool LoadFromFile(const char* shaderName, Introspection introspection, StringView vertexPath, DefaultFragment fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool LoadFromFile(const char* shaderName, StringView vertexPath, DefaultFragment fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);
		bool LoadFromFile(StringView vertexPath, DefaultFragment fragment, std::int32_t batchSize = GLShaderProgram::DefaultBatchSize);

		bool LoadFromCache(const char* shaderName, std::uint64_t shaderVersion, Introspection introspection);

		bool SaveToCache(const char* shaderName, std::uint64_t shaderVersion) const;

		/// Sets the VBO stride and pointer for the specified vertex attribute
		bool SetAttribute(const char* name, std::int32_t stride, void* pointer);

		/// Returns `true` if the shader is linked and can therefore be used
		bool IsLinked() const;

		/// Returns the length of the information log including the null termination character
		unsigned int RetrieveInfoLogLength() const;
		/// Retrieves the information log and copies it in the provided string object
		void RetrieveInfoLog(std::string& infoLog) const;

		/// Returns the automatic log on errors flag
		bool GetLogOnErrors() const;
		/// Sets the automatic log on errors flag
		/*! If the flag is true the shader will automatically log compilation and linking errors. */
		void SetLogOnErrors(bool shouldLogOnErrors);

		/// Sets the OpenGL object label for the shader program
		void SetGLShaderProgramLabel(const char* label);

		/// Registers a shaders to be used for batches of render commands
		void RegisterBatchedShader(Shader& batchedShader);

		GLShaderProgram* GetHandle() {
			return glShaderProgram_.get();
		}

		inline static ObjectType sType() {
			return ObjectType::Shader;
		}

	private:
		/// OpenGL shader program
		std::unique_ptr<GLShaderProgram> glShaderProgram_;

		bool LoadDefaultShader(DefaultVertex vertex, int batchSize);
		bool LoadDefaultShader(DefaultFragment fragment);
	};
}