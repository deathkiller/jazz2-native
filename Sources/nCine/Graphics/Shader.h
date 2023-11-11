#pragma once

#include "GL/GLShaderProgram.h"
#include "../Base/Object.h"

#include <memory>
#include <string>

namespace nCine
{
	/// Shader class
	class Shader : public Object
	{
	public:
		enum class LoadMode {
			String,
			File
		};

		enum class Introspection {
			Enabled,
			NoUniformsInBlocks,
			Disabled
		};

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

		enum class DefaultFragment {
			SPRITE,
			//SPRITE_GRAY,
			SPRITE_NOTEXTURE,
			//TEXTNODE_ALPHA,
			//TEXTNODE_RED,
		};

		Shader();

		Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, const char* vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		Shader(const char* shaderName, LoadMode loadMode, const char* vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		Shader(LoadMode loadMode, const char* vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);

		Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, DefaultVertex vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		Shader(const char* shaderName, LoadMode loadMode, DefaultVertex vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		Shader(LoadMode loadMode, DefaultVertex vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, const char* vertex, DefaultFragment fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		Shader(const char* shaderName, LoadMode loadMode, const char* vertex, DefaultFragment fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		Shader(LoadMode loadMode, const char* vertex, DefaultFragment fragment, int batchSize = GLShaderProgram::DefaultBatchSize);

		~Shader() override;

		bool loadFromMemory(const char* shaderName, Introspection introspection, const char* vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromMemory(const char* shaderName, const char* vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromMemory(const char* vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);

		bool loadFromMemory(const char* shaderName, Introspection introspection, DefaultVertex vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromMemory(const char* shaderName, DefaultVertex vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromMemory(DefaultVertex vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromMemory(const char* shaderName, Introspection introspection, const char* vertex, DefaultFragment fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromMemory(const char* shaderName, const char* vertex, DefaultFragment fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromMemory(const char* vertex, DefaultFragment fragment, int batchSize = GLShaderProgram::DefaultBatchSize);

		bool loadFromFile(const char* shaderName, Introspection introspection, const char* vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromFile(const char* shaderName, const char* vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromFile(const char* vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);

		bool loadFromFile(const char* shaderName, Introspection introspection, DefaultVertex vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromFile(const char* shaderName, DefaultVertex vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromFile(DefaultVertex vertex, const char* fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromFile(const char* shaderName, Introspection introspection, const char* vertex, DefaultFragment fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromFile(const char* shaderName, const char* vertex, DefaultFragment fragment, int batchSize = GLShaderProgram::DefaultBatchSize);
		bool loadFromFile(const char* vertex, DefaultFragment fragment, int batchSize = GLShaderProgram::DefaultBatchSize);

		bool loadFromCache(const char* shaderName, uint64_t shaderVersion, Introspection introspection);

		bool saveToCache(const char* shaderName, uint64_t shaderVersion) const;

		/// Sets the VBO stride and pointer for the specified vertex attribute
		bool setAttribute(const char* name, int stride, void* pointer);

		/// Returns true if the shader is linked and can therefore be used
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
		/// The OpenGL shader program
		std::unique_ptr<GLShaderProgram> glShaderProgram_;

		bool loadDefaultShader(DefaultVertex vertex, int batchSize);
		bool loadDefaultShader(DefaultFragment fragment);

		/// Deleted copy constructor
		Shader(const Shader&) = delete;
		/// Deleted assignment operator
		Shader& operator=(const Shader&) = delete;

		friend class ShaderState;
		friend class Material;
	};
}