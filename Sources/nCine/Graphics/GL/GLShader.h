#pragma once

#include "../../../Main.h"

#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	/// Handles OpenGL shader objects
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
		GLShader(GLenum type, StringView filename);
		~GLShader();

		inline GLuint glHandle() const {
			return glHandle_;
		}
		inline Status status() const {
			return status_;
		}

		/// Loads a shader from the given string
		bool loadFromString(StringView string);
		/// Loads a shader from the given string and then append the specified file
		bool loadFromStringAndFile(StringView string, StringView filename);
		/// Loads a shader by concatenating the given strings in order
		bool loadFromStrings(ArrayView<const StringView> strings);
		/// Loads a shader by concatenating the given strings in order, then appending the specified file
		bool loadFromStringsAndFile(ArrayView<const StringView> strings, StringView filename);
		/// Loads a shader from the specified file
		bool loadFromFile(StringView filename);

		bool compile(ErrorChecking errorChecking, bool logOnErrors);
		bool checkCompilation(bool logOnErrors);

		void setObjectLabel(const char* label);

	private:
		static constexpr std::uint32_t MaxShaderSourceLength = 32 * 1024;
#if defined(DEATH_TRACE)
		static constexpr std::uint32_t MaxInfoLogLength = 512;
		static char infoLogString_[MaxInfoLogLength];
#endif

		GLuint glHandle_;
		Status status_;

		GLShader(const GLShader&) = delete;
		GLShader& operator=(const GLShader&) = delete;
	};

}
