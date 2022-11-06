#pragma once

#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"

namespace nCine
{
	/// A class to store information about an active OpenGL shader attribute
	class GLAttribute
	{
	public:
		GLAttribute();
		GLAttribute(GLuint program, GLuint index);

		inline GLint location() const {
			return location_;
		}
		inline GLint size() const {
			return size_;
		}
		inline GLenum type() const {
			return type_;
		}
		inline const char* name() const {
			return name_;
		}
		GLenum basicType() const;
		int numComponents() const;

		/// Returns true if the attribute name starts with `gl_`
		bool hasReservedPrefix() const;

	private:
		GLint location_;
		GLint size_;
		GLenum type_;
		static constexpr int MaxNameLength = 32;
		char name_[MaxNameLength];
	};

}
