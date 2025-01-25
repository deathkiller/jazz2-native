#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

#include "GLUniform.h"
#include "GLUniformBlock.h"
#include "GLAttribute.h"
#include "GLVertexFormat.h"
#include "../../Base/StaticHashMap.h"
#include "../../../Main.h"

#include <string>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class GLShader;

	/// Handles OpenGL shader programs
	class GLShaderProgram
	{
		friend class GLShaderUniforms;
		friend class GLShaderUniformBlocks;

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

		static constexpr std::int32_t DefaultBatchSize = -1;

		GLShaderProgram();
		explicit GLShaderProgram(QueryPhase queryPhase);
		GLShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase);
		GLShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection);
		GLShaderProgram(StringView vertexFile, StringView fragmentFile);
		~GLShaderProgram();

		GLShaderProgram(const GLShaderProgram&) = delete;
		GLShaderProgram& operator=(const GLShaderProgram&) = delete;

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
		inline std::uint32_t batchSize() const {
			return batchSize_;
		}
		inline void setBatchSize(std::uint32_t value) {
			batchSize_ = value;
		}

		bool isLinked() const;

		/// Returns the length of the information log including the null termination character
		std::uint32_t retrieveInfoLogLength() const;
		/// Retrieves the information log and copies it in the provided string object
		void retrieveInfoLog(std::string& infoLog) const;

		/// Returns the total memory needed for all uniforms outside of blocks
		inline std::uint32_t uniformsSize() const {
			return uniformsSize_;
		}
		/// Returns the total memory needed for all uniforms inside of blocks
		inline std::uint32_t uniformBlocksSize() const {
			return uniformBlocksSize_;
		}

		bool attachShaderFromFile(GLenum type, StringView filename);
		bool attachShaderFromString(GLenum type, StringView string);
		bool attachShaderFromStrings(GLenum type, ArrayView<const StringView> strings);
		bool attachShaderFromStringsAndFile(GLenum type, ArrayView<const StringView> strings, StringView filename);
		bool link(Introspection introspection);
		void use();
		bool validate();

		bool finalizeAfterLinking(Introspection introspection);

		inline std::uint32_t numAttributes() const {
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
		static constexpr std::uint32_t MaxNumUniforms = 32;

		static constexpr std::int32_t AttachedShadersInitialSize = 2;
		static constexpr std::int32_t UniformsInitialSize = 8;
		static constexpr std::int32_t UniformBlocksInitialSize = 4;
		static constexpr std::int32_t AttributesInitialSize = 4;

#if defined(DEATH_TRACE)
		static constexpr std::uint32_t MaxInfoLogLength = 512;
		static char infoLogString_[MaxInfoLogLength];
#endif

		static GLuint boundProgram_;

		GLuint glHandle_;

		SmallVector<std::unique_ptr<GLShader>, AttachedShadersInitialSize> attachedShaders_;
		Status status_;
		Introspection introspection_;
		QueryPhase queryPhase_;
		std::uint32_t batchSize_;

		/// A flag indicating whether the shader program should automatically log errors (the information log)
		bool shouldLogOnErrors_;

		std::uint32_t uniformsSize_;
		std::uint32_t uniformBlocksSize_;

		SmallVector<GLUniform, 0> uniforms_;
		SmallVector<GLUniformBlock, 0> uniformBlocks_;
		SmallVector<GLAttribute, 0> attributes_;

		StaticHashMap<String, std::int32_t, GLVertexFormat::MaxAttributes> attributeLocations_;
		GLVertexFormat vertexFormat_;

		bool deferredQueries();
		bool checkLinking();
		void performIntrospection();

		void discoverUniforms();
		void discoverUniformBlocks(GLUniformBlock::DiscoverUniforms discover);
		void discoverAttributes();
		void initVertexFormat();
	};
}
