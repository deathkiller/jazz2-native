#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

#include "../../../Main.h"

namespace nCine
{
	/// Stores information about an active OpenGL shader uniform
	/*! Its only purpose is to initialize a `GLUniformCache` class. */
	class GLUniform
	{
		friend class GLUniformBlock;

	public:
		static constexpr std::uint32_t MaxNameLength = 48;

		GLUniform();
		GLUniform(GLuint program, GLuint index);

		inline GLuint index() const {
			return index_;
		}
		inline GLint blockIndex() const {
			return blockIndex_;
		}
		inline GLint location() const {
			return location_;
		}
		inline GLint size() const {
			return size_;
		}
		inline GLenum type() const {
			return type_;
		}
		inline GLint offset() const {
			return offset_;
		}
		inline const char* name() const {
			return name_;
		}
		GLenum basicType() const;
		std::uint32_t numComponents() const;
		inline std::uint32_t memorySize() const {
			return size() * numComponents() * sizeof(basicType());
		}

		/// Returns true if the uniform name starts with `gl_`
		bool hasReservedPrefix() const;

	private:
		GLuint index_;
		/// Active uniforms not in a block have a block index of -1
		GLint blockIndex_;
		GLint location_;
		GLint size_;
		GLenum type_;
		GLint offset_;
		char name_[MaxNameLength];
	};

}
