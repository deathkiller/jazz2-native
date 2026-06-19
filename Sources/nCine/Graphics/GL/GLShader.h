#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

#include "../../../Main.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Wraps an OpenGL shader object
		
		Manages the lifetime of a single shader object of a given stage (e.g., vertex or fragment),
		loads its source from strings and/or a file, compiles it, and tracks the compilation status.
		Compilation errors can be checked immediately or deferred.
	*/
	class GLShader
	{
	public:
		/** @brief Compilation status of the shader */
		enum class Status
		{
			NotCompiled,			/**< The shader has not been compiled yet */
			CompilationFailed,		/**< Compilation failed */
			Compiled,				/**< Compiled successfully */
			CompiledWithDeferredChecks	/**< Compiled, but the success status has not been checked yet */
		};

		/** @brief When the compilation status is checked */
		enum class ErrorChecking
		{
			Immediate,	/**< The compilation status is checked right after compiling */
			Deferred	/**< The compilation status check is postponed */
		};

		explicit GLShader(GLenum type);
		GLShader(GLenum type, StringView filename);
		~GLShader();

		GLShader(const GLShader&) = delete;
		GLShader& operator=(const GLShader&) = delete;

		/** @brief Returns the OpenGL handle of the shader object */
		inline GLuint GetGLHandle() const {
			return glHandle_;
		}
		/** @brief Returns the current compilation status */
		inline Status GetStatus() const {
			return status_;
		}

		/** @brief Loads the shader source from the given string */
		bool LoadFromString(StringView string);
		/** @brief Loads the shader source from the given string, then appends the specified file */
		bool LoadFromStringAndFile(StringView string, StringView filename);
		/** @brief Loads the shader source by concatenating the given strings in order */
		bool LoadFromStrings(ArrayView<const StringView> strings);
		/** @brief Loads the shader source by concatenating the given strings in order, then appends the specified file */
		bool LoadFromStringsAndFile(ArrayView<const StringView> strings, StringView filename);
		/** @brief Loads the shader source from the specified file */
		bool LoadFromFile(StringView filename);

		/**
		 * @brief Compiles the shader
		 *
		 * @param errorChecking	Whether to check the compilation status immediately or defer it
		 * @param logOnErrors	Whether to log the information log when compilation fails
		 * @return `true` on success, or when the status check is deferred
		 */
		bool Compile(ErrorChecking errorChecking, bool logOnErrors);
		/**
		 * @brief Checks the result of a deferred compilation
		 *
		 * @param logOnErrors	Whether to log the information log when compilation failed
		 * @return `true` if the shader compiled successfully
		 */
		bool CheckCompilation(bool logOnErrors);

		/** @brief Sets an OpenGL object label for the shader, for debugging */
		void SetObjectLabel(StringView label);

	private:
		static constexpr std::uint32_t MaxShaderSourceLength = 32 * 1024;

		GLuint glHandle_;
		Status status_;
	};
}
