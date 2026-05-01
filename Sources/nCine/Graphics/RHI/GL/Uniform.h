#pragma once

#if defined(WITH_RHI_GL)

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "../../../../Main.h"

namespace nCine::RHI
{
	/// Stores information about an active OpenGL shader uniform
	/*! Its only purpose is to initialize a `UniformCache` class. */
	class Uniform
	{
		friend class UniformBlock;

	public:
		static constexpr std::uint32_t MaxNameLength = 48;

		Uniform();
		Uniform(GLuint program, GLuint index);

		inline GLuint GetIndex() const {
			return index_;
		}
		inline GLint GetBlockIndex() const {
			return blockIndex_;
		}
		inline GLint GetLocation() const {
			return location_;
		}
		inline GLint GetSize() const {
			return size_;
		}
		inline GLenum GetType() const {
			return type_;
		}
		inline GLint GetOffset() const {
			return offset_;
		}
		inline const char* GetName() const {
			return name_;
		}
		GLenum GetBasicType() const;
		std::uint32_t GetComponentCount() const;
		inline std::uint32_t GetMemorySize() const {
			return GetSize() * GetComponentCount() * sizeof(GetBasicType());
		}

		/// Returns true if the uniform name starts with `gl_`
		bool HasReservedPrefix() const;

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

#endif // defined(WITH_RHI_GL)
