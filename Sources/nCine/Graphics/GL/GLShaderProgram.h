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

	/**
		@brief Wraps an OpenGL shader program object
		
		Manages the lifetime of a shader program, attaches @ref GLShader objects, links and optionally
		validates them, and binds the program for rendering (caching the currently bound program to
		avoid redundant `glUseProgram()` calls). After linking it can introspect the program to discover
		active uniforms, uniform blocks and vertex attributes, and to build the associated vertex format.
		Compilation and linking queries can be performed immediately or deferred until the program is first used.
	*/
	class GLShaderProgram
	{
		friend class GLShaderUniforms;
		friend class GLShaderUniformBlocks;

	public:
		/** @brief How much of the program is introspected after linking */
		enum class Introspection
		{
			Enabled,			/**< Discover all uniforms, including those inside uniform blocks */
			NoUniformsInBlocks,	/**< Discover uniforms and uniform blocks, but not the uniforms inside the blocks */
			Disabled			/**< Perform no introspection */
		};

		/** @brief Lifecycle status of the shader program */
		enum class Status
		{
			NotLinked,					/**< Not linked yet */
			CompilationFailed,			/**< Compilation of an attached shader failed */
			LinkingFailed,				/**< Linking failed */
			Linked,						/**< Linked successfully */
			LinkedWithDeferredQueries,	/**< Linked, but the status checks and introspection are still pending */
			LinkedWithIntrospection		/**< Linked and introspected */
		};

		/** @brief When compilation and linking status are queried */
		enum class QueryPhase
		{
			Immediate,	/**< Status is checked and introspection is performed right after linking */
			Deferred	/**< Status checks and introspection are postponed until the program is first used */
		};

		/** @brief Default batch size, indicating the shader is not batched */
		static constexpr std::int32_t DefaultBatchSize = -1;

		GLShaderProgram();
		explicit GLShaderProgram(QueryPhase queryPhase);
		GLShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase);
		GLShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection);
		GLShaderProgram(StringView vertexFile, StringView fragmentFile);
		~GLShaderProgram();

		GLShaderProgram(const GLShaderProgram&) = delete;
		GLShaderProgram& operator=(const GLShaderProgram&) = delete;

		/** @brief Returns the OpenGL handle of the shader program object */
		inline GLuint GetGLHandle() const {
			return glHandle_;
		}
		/** @brief Returns the current lifecycle status */
		inline Status GetStatus() const {
			return status_;
		}
		/** @brief Returns the introspection level used when linking */
		inline Introspection GetIntrospection() const {
			return introspection_;
		}
		/** @brief Returns when compilation and linking status are queried */
		inline QueryPhase GetQueryPhase() const {
			return queryPhase_;
		}
		/** @brief Returns the batch size, or @ref DefaultBatchSize if the shader is not batched */
		inline std::uint32_t GetBatchSize() const {
			return batchSize_;
		}
		/** @brief Sets the batch size used for batched rendering */
		inline void SetBatchSize(std::uint32_t value) {
			batchSize_ = value;
		}

		/** @brief Returns `true` if the program has been linked successfully */
		bool IsLinked() const;

		/** @brief Returns the length of the information log, including the null terminator */
		std::uint32_t RetrieveInfoLogLength() const;
		/**
		 * @brief Retrieves the information log
		 *
		 * Copies the program information log into the provided string, up to its current capacity.
		 */
		void RetrieveInfoLog(std::string& infoLog) const;

		/** @brief Returns the total memory needed for all uniforms outside of blocks */
		inline std::uint32_t GetUniformsSize() const {
			return uniformsSize_;
		}
		/** @brief Returns the total memory needed for all uniforms inside of blocks */
		inline std::uint32_t GetUniformBlocksSize() const {
			return uniformBlocksSize_;
		}

		/**
		 * @brief Attaches a shader stage compiled from the specified file
		 *
		 * @param type		The shader stage (e.g. `GL_VERTEX_SHADER`)
		 * @param filename	Path of the file containing the shader source
		 * @return `true` if the shader compiled successfully (or when checks are deferred)
		 */
		bool AttachShaderFromFile(GLenum type, StringView filename);
		/**
		 * @brief Attaches a shader stage compiled from the specified source string
		 *
		 * @param type		The shader stage (e.g. `GL_VERTEX_SHADER`)
		 * @param string	The shader source
		 * @return `true` if the shader compiled successfully (or when checks are deferred)
		 */
		bool AttachShaderFromString(GLenum type, StringView string);
		/**
		 * @brief Attaches a shader stage compiled from the specified source strings
		 *
		 * @param type		The shader stage (e.g. `GL_VERTEX_SHADER`)
		 * @param strings	The shader source fragments, concatenated in order
		 * @return `true` if the shader compiled successfully (or when checks are deferred)
		 */
		bool AttachShaderFromStrings(GLenum type, ArrayView<const StringView> strings);
		/**
		 * @brief Attaches a shader stage compiled from the specified source strings and file
		 *
		 * @param type		The shader stage (e.g. `GL_VERTEX_SHADER`)
		 * @param strings	The shader source fragments, concatenated in order
		 * @param filename	Path of a file whose source is appended after the strings
		 * @return `true` if the shader compiled successfully (or when checks are deferred)
		 */
		bool AttachShaderFromStringsAndFile(GLenum type, ArrayView<const StringView> strings, StringView filename);
		/**
		 * @brief Links the attached shaders into the program
		 *
		 * @param introspection	The introspection level to use after linking
		 * @return `true` on success, or when the status check is deferred
		 */
		bool Link(Introspection introspection);
		/**
		 * @brief Binds the program for rendering
		 *
		 * Processes any deferred queries on first use and calls `glUseProgram()`, skipping it if the
		 * program is already bound.
		 */
		void Use();
		/** @brief Validates the program and returns `true` if validation succeeded */
		bool Validate();

		/**
		 * @brief Finalizes the program after an external link step
		 *
		 * When queries are immediate, checks the link status, detaches the shader objects and performs
		 * introspection; otherwise marks the program for deferred queries.
		 *
		 * @param introspection	The introspection level to use
		 * @return `true` on success, or when the status check is deferred
		 */
		bool FinalizeAfterLinking(Introspection introspection);

		/** @brief Returns the number of active vertex attributes discovered by introspection */
		inline std::uint32_t GetAttributeCount() const {
			return attributeLocations_.size();
		}
		/** @brief Returns `true` if the program has an active vertex attribute with the given name */
		inline bool HasAttribute(const char* name) const {
			return (attributeLocations_.find(String::nullTerminatedView(name)) != nullptr);
		}
		/** @brief Returns the vertex format attribute with the given name, or `nullptr` if not found */
		GLVertexFormat::Attribute* GetAttribute(const char* name);

		/** @brief Defines the vertex format from a vertex buffer object */
		inline void DefineVertexFormat(const GLBufferObject* vbo) {
			DefineVertexFormat(vbo, nullptr, 0);
		}
		/** @brief Defines the vertex format from a vertex buffer object and an index buffer object */
		inline void DefineVertexFormat(const GLBufferObject* vbo, const GLBufferObject* ibo) {
			DefineVertexFormat(vbo, ibo, 0);
		}
		/**
		 * @brief Defines the vertex format from the given buffer objects
		 *
		 * Assigns the vertex and index buffers to the introspected attributes and binds a matching VAO.
		 *
		 * @param vbo		The vertex buffer object providing attribute data
		 * @param ibo		The index buffer object, or `nullptr` if unused
		 * @param vboOffset	Base offset into the vertex buffer applied to every attribute
		 */
		void DefineVertexFormat(const GLBufferObject* vbo, const GLBufferObject* ibo, std::uint32_t vboOffset);

		/**
		 * @brief Resets the program so that new shaders can be attached
		 *
		 * Clears the introspection state, deletes the current OpenGL program and creates a fresh one.
		 */
		void Reset();

		/** @brief Sets an OpenGL object label for the program, for debugging */
		void SetObjectLabel(StringView label);

		/** @brief Returns the automatic log on errors flag */
		inline bool GetLogOnErrors() const {
			return shouldLogOnErrors_;
		}
		/**
		 * @brief Sets the automatic log on errors flag
		 *
		 * When `true`, the shader program automatically logs compilation and linking errors.
		 */
		inline void SetLogOnErrors(bool shouldLogOnErrors) {
			shouldLogOnErrors_ = shouldLogOnErrors;
		}

	private:
		/** @brief Maximum number of discoverable uniforms outside of blocks */
		static constexpr std::uint32_t MaxNumUniforms = 32;

		static constexpr std::int32_t AttachedShadersInitialSize = 2;
		static constexpr std::int32_t UniformsInitialSize = 8;
		static constexpr std::int32_t UniformBlocksInitialSize = 4;
		static constexpr std::int32_t AttributesInitialSize = 4;

		static GLuint boundProgram_;

		GLuint glHandle_;

		SmallVector<std::unique_ptr<GLShader>, AttachedShadersInitialSize> attachedShaders_;
		Status status_;
		Introspection introspection_;
		QueryPhase queryPhase_;
		std::uint32_t batchSize_;

		/** @brief Whether the shader program should automatically log errors (the information log) */
		bool shouldLogOnErrors_;

		std::uint32_t uniformsSize_;
		std::uint32_t uniformBlocksSize_;

		SmallVector<GLUniform, 0> uniforms_;
		SmallVector<GLUniformBlock, 0> uniformBlocks_;
		SmallVector<GLAttribute, 0> attributes_;

		StaticHashMap<String, std::int32_t, GLVertexFormat::MaxAttributes> attributeLocations_;
		GLVertexFormat vertexFormat_;

		bool ProcessDeferredQueries();
		bool CheckLinking();
		void PerformIntrospection();

		void DiscoverUniforms();
		void DiscoverUniformBlocks(GLUniformBlock::DiscoverUniforms discover);
		void DiscoverAttributes();
		void InitVertexFormat();
	};
}
