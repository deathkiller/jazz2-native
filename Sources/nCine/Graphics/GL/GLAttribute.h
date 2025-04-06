#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

#include "../../../Main.h"

namespace nCine
{
	/// Stores information about an active OpenGL shader attribute
	class GLAttribute
	{
	public:
		GLAttribute();
		GLAttribute(GLuint program, GLuint index);

		inline GLint GetLocation() const {
			return location_;
		}
		inline GLint GetSize() const {
			return size_;
		}
		inline GLenum GetType() const {
			return type_;
		}
		inline const char* GetName() const {
			return name_;
		}
		GLenum GetBasicType() const;
		std::int32_t GetComponentCount() const;

		/// Returns true if the attribute name starts with `gl_`
		bool HasReservedPrefix() const;

	private:
		GLint location_;
		GLint size_;
		GLenum type_;
		static constexpr std::int32_t MaxNameLength = 32;
		char name_[MaxNameLength];
	};

}
