#pragma once

#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"

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
		GLShader(GLenum type, const char* filename);
		~GLShader();

		inline GLuint glHandle() const {
			return glHandle_;
		}
		inline Status status() const {
			return status_;
		}

		void loadFromString(const char* string);
		void loadFromFile(const char* filename);
		void compile(ErrorChecking errorChecking);

		bool checkCompilation();

	private:
		GLuint glHandle_;
		Status status_;

		/// Deleted copy constructor
		GLShader(const GLShader&) = delete;
		/// Deleted assignment operator
		GLShader& operator=(const GLShader&) = delete;
	};

}
