#pragma once

#include "RHI/Rhi.h"
#include "../Base/Object.h"

#include <memory>
#include <string>

namespace nCine
{
	/**
		@brief GPU shader program usable by materials and drawable nodes
		
		Wraps an OpenGL shader program built from a vertex and a fragment shader. The sources can
		come from memory, a file, one of the built-in default shaders or the binary cache, and a
		matching variant can be registered for rendering batched draw commands.
	*/
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

		Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, const char* vertex, const char* fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);
		Shader(const char* shaderName, LoadMode loadMode, const char* vertex, const char* fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);
		Shader(LoadMode loadMode, const char* vertex, const char* fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);

		Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, DefaultVertex vertex, const char* fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);
		Shader(const char* shaderName, LoadMode loadMode, DefaultVertex vertex, const char* fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);
		Shader(LoadMode loadMode, DefaultVertex vertex, const char* fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);
		Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, const char* vertex, DefaultFragment fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);
		Shader(const char* shaderName, LoadMode loadMode, const char* vertex, DefaultFragment fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);
		Shader(LoadMode loadMode, const char* vertex, DefaultFragment fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);

		~Shader() override;

		Shader(const Shader&) = delete;
		Shader& operator=(const Shader&) = delete;

		bool LoadFromMemory(const char* shaderName, Introspection introspection, const char* vertex, const char* fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool LoadFromMemory(const char* shaderName, const char* vertex, const char* fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);
		bool LoadFromMemory(const char* vertex, const char* fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);

		bool LoadFromMemory(const char* shaderName, Introspection introspection, DefaultVertex vertex, const char* fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool LoadFromMemory(const char* shaderName, DefaultVertex vertex, const char* fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);
		bool LoadFromMemory(DefaultVertex vertex, const char* fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);
		bool LoadFromMemory(const char* shaderName, Introspection introspection, const char* vertex, DefaultFragment fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool LoadFromMemory(const char* shaderName, const char* vertex, DefaultFragment fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);
		bool LoadFromMemory(const char* vertex, DefaultFragment fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);

		bool LoadFromFile(const char* shaderName, Introspection introspection, StringView vertexPath, StringView fragmentPath, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool LoadFromFile(const char* shaderName, StringView vertexPath, StringView fragmentPath, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);
		bool LoadFromFile(StringView vertexPath, StringView fragmentPath, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);

		bool LoadFromFile(const char* shaderName, Introspection introspection, DefaultVertex vertex, StringView fragmentPath, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool LoadFromFile(const char* shaderName, DefaultVertex vertex, StringView fragmentPath, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);
		bool LoadFromFile(DefaultVertex vertex, StringView fragmentPath, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);
		bool LoadFromFile(const char* shaderName, Introspection introspection, StringView vertexPath, DefaultFragment fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool LoadFromFile(const char* shaderName, StringView vertexPath, DefaultFragment fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);
		bool LoadFromFile(StringView vertexPath, DefaultFragment fragment, std::int32_t batchSize = Rhi::ShaderProgram::DefaultBatchSize);

		bool LoadFromCache(const char* shaderName, std::uint64_t shaderVersion, Introspection introspection);

		bool SaveToCache(const char* shaderName, std::uint64_t shaderVersion) const;

		/** @brief Sets the VBO stride and pointer for the specified vertex attribute */
		bool SetAttribute(const char* name, std::int32_t stride, void* pointer);

		/** @brief Returns `true` if the shader is linked and can therefore be used */
		bool IsLinked() const;

		/** @brief Returns the length of the information log, including the null terminator */
		unsigned int RetrieveInfoLogLength() const;
		/** @brief Retrieves the information log and copies it into the provided string object */
		void RetrieveInfoLog(std::string& infoLog) const;

		/** @brief Returns the automatic log on errors flag */
		bool GetLogOnErrors() const;
		/**
		 * @brief Sets the automatic log on errors flag
		 *
		 * When the flag is `true` the shader automatically logs compilation and linking errors.
		 */
		void SetLogOnErrors(bool shouldLogOnErrors);

		/** @brief Sets the OpenGL object label for the shader program */
		void SetGLShaderProgramLabel(const char* label);

		/** @brief Registers a shader to be used when rendering batches of render commands */
		void RegisterBatchedShader(Shader& batchedShader);

		Rhi::ShaderProgram* GetHandle() {
			return glShaderProgram_.get();
		}

		inline static ObjectType sType() {
			return ObjectType::Shader;
		}

	private:
		/** @brief Underlying OpenGL shader program */
		std::unique_ptr<Rhi::ShaderProgram> glShaderProgram_;

		bool LoadDefaultShader(DefaultVertex vertex, int batchSize);
		bool LoadDefaultShader(DefaultFragment fragment);
	};
}