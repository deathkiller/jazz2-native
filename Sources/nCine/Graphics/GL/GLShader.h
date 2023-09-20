#pragma once

#include "../../../Common.h"

#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	/// A class to handle OpenGL shader objects
	class GLShader
	{
	public:
		enum class Status
		{
			NotCompiled,
			CompilationFailed,
			Compiled,
			CompiledWithDeferredChecks
		};

		enum class ErrorChecking
		{
			Immediate,
			Deferred
		};

		explicit GLShader(GLenum type);
		GLShader(GLenum type, const StringView& filename);
		~GLShader();

		inline GLuint glHandle() const {
			return glHandle_;
		}
		inline Status status() const {
			return status_;
		}

		/// Loads a shader from the given string
		bool loadFromString(const char* string);
		/// Loads a shader from the given string and then append the specified file
		bool loadFromStringAndFile(const char* string, const StringView& filename);
		/// Loads a shader by concatenating the given strings in order
		bool loadFromStrings(const char** strings);
		/// Loads a shader by concatenating the given strings in order, then appending the specified file
		bool loadFromStringsAndFile(const char** strings, const StringView& filename);
		/// Loads a shader from the specified file
		bool loadFromFile(const StringView& filename);

		bool compile(ErrorChecking errorChecking, bool logOnErrors);
		bool checkCompilation(bool logOnErrors);

		void setObjectLabel(const char* label);

	private:
		static constexpr unsigned int MaxShaderSourceLength = 32 * 1024;
#if defined(DEATH_TRACE)
		static constexpr unsigned int MaxInfoLogLength = 512;
		static char infoLogString_[MaxInfoLogLength];
#endif

		GLuint glHandle_;
		Status status_;

		/// Deleted copy constructor
		GLShader(const GLShader&) = delete;
		/// Deleted assignment operator
		GLShader& operator=(const GLShader&) = delete;
	};

}
