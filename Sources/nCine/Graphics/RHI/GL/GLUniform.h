#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "../../../../Main.h"

namespace nCine::RhiGL
{
	/**
		@brief Stores information about an active shader uniform
		
		Holds the metadata queried for a single active uniform of a linked shader program,
		such as its location, array size, GL type, name and (when the uniform lives inside a
		uniform block) its owning block index and byte offset. It carries no value storage of
		its own and is mainly used to initialize a @ref GLUniformCache.
	*/
	class GLUniform
	{
		friend class GLUniformBlock;

	public:
		/** @brief Maximum length of a uniform name, including the terminating null character */
		static constexpr std::uint32_t MaxNameLength = 48;

		/** @brief Creates an empty, unbound uniform */
		GLUniform();
		/** @brief Queries the active uniform at the specified index of a linked program */
		GLUniform(GLuint program, GLuint index);

		/** @brief Returns the active uniform index within the program */
		inline GLuint GetIndex() const {
			return index_;
		}
		/** @brief Returns the index of the owning uniform block, or -1 if the uniform is not in a block */
		inline GLint GetBlockIndex() const {
			return blockIndex_;
		}
		/** @brief Returns the uniform location, or -1 if it has no location (e.g., block members) */
		inline GLint GetLocation() const {
			return location_;
		}
		/** @brief Returns the number of array elements (1 for non-array uniforms) */
		inline GLint GetSize() const {
			return size_;
		}
		/** @brief Returns the GL type enum of the uniform (e.g., `GL_FLOAT_VEC4`) */
		inline GLenum GetType() const {
			return type_;
		}
		/** @brief Returns the byte offset of the uniform within its uniform block */
		inline GLint GetOffset() const {
			return offset_;
		}
		/** @brief Returns the uniform name */
		inline const char* GetName() const {
			return name_;
		}
		/** @brief Returns the basic component type (`GL_FLOAT`, `GL_INT` or `GL_BOOL`) derived from the GL type */
		GLenum GetBasicType() const;
		/** @brief Returns the number of components of the uniform type (e.g., 4 for a `vec4`, 16 for a `mat4`) */
		std::uint32_t GetComponentCount() const;
		/** @brief Returns the size in bytes occupied by the uniform value */
		inline std::uint32_t GetMemorySize() const {
			return GetSize() * GetComponentCount() * sizeof(GetBasicType());
		}

		/** @brief Returns `true` if the uniform name starts with the reserved `gl_` prefix */
		bool HasReservedPrefix() const;

	private:
		GLuint index_;
		// Active uniforms not in a block have a block index of -1
		GLint blockIndex_;
		GLint location_;
		GLint size_;
		GLenum type_;
		GLint offset_;
		char name_[MaxNameLength];
	};

}
