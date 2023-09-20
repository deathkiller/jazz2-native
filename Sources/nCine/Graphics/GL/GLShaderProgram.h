#pragma once

#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"

#include "GLUniform.h"
#include "GLUniformBlock.h"
#include "GLAttribute.h"
#include "GLVertexFormat.h"
#include "../../Base/StaticHashMap.h"
#include "../../../Common.h"

#include <string>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class GLShader;

	/// A class to handle OpenGL shader programs
	class GLShaderProgram
	{
	public:
		enum class Introspection
		{
			Enabled,
			NoUniformsInBlocks,
			Disabled
		};

		enum class Status
		{
			NotLinked,
			CompilationFailed,
			LinkingFailed,
			Linked,
			LinkedWithDeferredQueries,
			LinkedWithIntrospection
		};

		enum class QueryPhase
		{
			Immediate,
			Deferred
		};

		static constexpr int DefaultBatchSize = -1;

		GLShaderProgram();
		explicit GLShaderProgram(QueryPhase queryPhase);
		GLShaderProgram(const StringView& vertexFile, const StringView& fragmentFile, Introspection introspection, QueryPhase queryPhase);
		GLShaderProgram(const StringView& vertexFile, const StringView& fragmentFile, Introspection introspection);
		GLShaderProgram(const StringView& vertexFile, const StringView& fragmentFile);
		~GLShaderProgram();

		inline GLuint glHandle() const {
			return glHandle_;
		}
		inline Status status() const {
			return status_;
		}
		inline Introspection introspection() const {
			return introspection_;
		}
		inline QueryPhase queryPhase() const {
			return queryPhase_;
		}
		inline unsigned int batchSize() const {
			return batchSize_;
		}
		inline void setBatchSize(int value) {
			batchSize_ = value;
		}

		bool isLinked() const;

		/// Returns the length of the information log including the null termination character
		unsigned int retrieveInfoLogLength() const;
		/// Retrieves the information log and copies it in the provided string object
		void retrieveInfoLog(std::string& infoLog) const;

		/// Returns the total memory needed for all uniforms outside of blocks
		inline unsigned int uniformsSize() const {
			return uniformsSize_;
		}
		/// Returns the total memory needed for all uniforms inside of blocks
		inline unsigned int uniformBlocksSize() const {
			return uniformBlocksSize_;
		}

		bool attachShaderFromFile(GLenum type, const StringView& filename);
		bool attachShaderFromString(GLenum type, const char* string);
		bool attachShaderFromStrings(GLenum type, const char** strings);
		bool attachShaderFromStringsAndFile(GLenum type, const char** strings, const StringView& filename);
		bool link(Introspection introspection);
		void use();
		bool validate();

		bool finalizeAfterLinking(Introspection introspection);

		inline unsigned int numAttributes() const {
			return attributeLocations_.size();
		}
		inline bool hasAttribute(const char* name) const {
			return (attributeLocations_.find(String::nullTerminatedView(name)) != nullptr);
		}
		GLVertexFormat::Attribute* attribute(const char* name);

		inline void defineVertexFormat(const GLBufferObject* vbo) {
			defineVertexFormat(vbo, nullptr, 0);
		}
		inline void defineVertexFormat(const GLBufferObject* vbo, const GLBufferObject* ibo) {
			defineVertexFormat(vbo, ibo, 0);
		}
		void defineVertexFormat(const GLBufferObject* vbo, const GLBufferObject* ibo, unsigned int vboOffset);

		/// Deletes the current OpenGL shader program so that new shaders can be attached
		void reset();

		void setObjectLabel(const char* label);

		/// Returns the automatic log on errors flag
		inline bool logOnErrors() const {
			return shouldLogOnErrors_;
		}
		/// Sets the automatic log on errors flag
		/*! If the flag is true the shader program will automatically log compilation and linking errors. */
		inline void setLogOnErrors(bool shouldLogOnErrors) {
			shouldLogOnErrors_ = shouldLogOnErrors;
		}

	private:
		/// Max number of discoverable uniforms
		static constexpr unsigned int MaxNumUniforms = 32;

		static constexpr int AttachedShadersInitialSize = 2;
		static constexpr int UniformsInitialSize = 8;
		static constexpr int UniformBlocksInitialSize = 4;
		static constexpr int AttributesInitialSize = 4;

#if defined(DEATH_TRACE)
		static constexpr unsigned int MaxInfoLogLength = 512;
		static char infoLogString_[MaxInfoLogLength];
#endif

		static GLuint boundProgram_;

		GLuint glHandle_;

		SmallVector<std::unique_ptr<GLShader>, AttachedShadersInitialSize> attachedShaders_;
		Status status_;
		Introspection introspection_;
		QueryPhase queryPhase_;
		unsigned int batchSize_;

		/// A flag indicating whether the shader program should automatically log errors (the information log)
		bool shouldLogOnErrors_;

		unsigned int uniformsSize_;
		unsigned int uniformBlocksSize_;

		SmallVector<GLUniform, 0> uniforms_;
		SmallVector<GLUniformBlock, 0> uniformBlocks_;
		SmallVector<GLAttribute, 0> attributes_;

		StaticHashMap<String, int, GLVertexFormat::MaxAttributes> attributeLocations_;
		GLVertexFormat vertexFormat_;

		bool deferredQueries();
		bool checkLinking();
		void performIntrospection();

		void discoverUniforms();
		void discoverUniformBlocks(GLUniformBlock::DiscoverUniforms discover);
		void discoverAttributes();
		void initVertexFormat();

		/// Deleted copy constructor
		GLShaderProgram(const GLShaderProgram&) = delete;
		/// Deleted assignment operator
		GLShaderProgram& operator=(const GLShaderProgram&) = delete;

		friend class GLShaderUniforms;
		friend class GLShaderUniformBlocks;
	};
}
