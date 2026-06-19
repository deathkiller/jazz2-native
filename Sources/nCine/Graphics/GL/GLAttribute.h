#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

#include "../../../Main.h"

namespace nCine
{
	/**
		@brief Stores information about an active vertex shader attribute
		
		Queried from a linked shader program with `glGetActiveAttrib()`, this holds the attribute's
		location, size, GL type and name. Attributes with a reserved `gl_` prefix are skipped and keep
		a location of -1.
	*/
	class GLAttribute
	{
	public:
		GLAttribute();
		GLAttribute(GLuint program, GLuint index);

		/** @brief Returns the attribute location, or -1 if it has no location */
		inline GLint GetLocation() const {
			return location_;
		}
		/** @brief Returns the number of components for array attributes (1 for non-array attributes) */
		inline GLint GetSize() const {
			return size_;
		}
		/** @brief Returns the GL type of the attribute (e.g., `GL_FLOAT_VEC4`) */
		inline GLenum GetType() const {
			return type_;
		}
		/** @brief Returns the attribute name */
		inline const char* GetName() const {
			return name_;
		}
		/**
		 * @brief Returns the basic GL type underlying the attribute type
		 *
		 * Maps a composite type to its scalar component type, e.g., `GL_FLOAT_VEC4` to `GL_FLOAT`.
		 */
		GLenum GetBasicType() const;
		/**
		 * @brief Returns the number of components in the attribute type
		 *
		 * For example, 4 for `GL_FLOAT_VEC4` and 1 for a scalar type.
		 */
		std::int32_t GetComponentCount() const;

		/** @brief Returns `true` if the attribute name starts with the reserved `gl_` prefix */
		bool HasReservedPrefix() const;

	private:
		GLint location_;
		GLint size_;
		GLenum type_;
		static constexpr std::int32_t MaxNameLength = 32;
		char name_[MaxNameLength];
	};

}
