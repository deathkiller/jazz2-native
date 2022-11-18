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
			NOT_COMPILED,
			COMPILATION_FAILED,
			COMPILED,
			COMPILED_WITH_DEFERRED_CHECKS
		};

		enum class ErrorChecking
		{
			IMMEDIATE,
			DEFERRED
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

		void loadFromString(const char* string);
		void loadFromFile(const StringView& filename);
		bool compile(ErrorChecking errorChecking, bool logOnErrors);

		bool checkCompilation(bool logOnErrors);

		void setObjectLabel(const char* label);

	private:
#if defined(NCINE_LOG)
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
