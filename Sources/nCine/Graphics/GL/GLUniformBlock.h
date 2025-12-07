#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

#include "GLUniform.h"
#include "../../Base/StaticHashMapIterator.h"

namespace nCine
{
	class GLShaderProgram;

	/// Stores information about an OpenGL uniform block
	class GLUniformBlock
	{
		friend class GLUniformBlockCache;

	public:
		static constexpr std::uint32_t MaxNameLength = 48;

		enum class DiscoverUniforms
		{
			ENABLED,
			DISABLED
		};

		GLUniformBlock();
		GLUniformBlock(GLuint program, GLuint blockIndex, DiscoverUniforms discover);
		GLUniformBlock(GLuint program, GLuint blockIndex);

		inline GLuint GetIndex() const {
			return index_;
		}
		inline GLint GetBindingIndex() const {
			return bindingIndex_;
		}
		/// Returns the size of the block aligned to the uniform buffer offset
		inline GLint GetSize() const {
			return size_;
		}
		/// Returns the uniform buffer offset alignment added to the original size
		inline std::uint8_t GetAlignAmount() const {
			return alignAmount_;
		}
		inline const char* GetName() const {
			return name_;
		}

		inline GLUniform* GetUniform(const char* name) {
			return blockUniforms_.find(String::nullTerminatedView(name));
		}
#if !defined(WITH_OPENGL2)
		void SetBlockBinding(GLuint blockBinding);
#endif

	private:
		/// Max number of discoverable uniforms per block
		static const std::int32_t MaxNumBlockUniforms = 16;

		static const std::uint32_t BlockUniformHashSize = 8;
		StaticHashMap<String, GLUniform, BlockUniformHashSize> blockUniforms_;

		GLuint program_;
		GLuint index_;
		/// Offset aligned size for `glBindBufferRange()` calls
		GLint size_;
		/// Uniform buffer offset alignment added to `size_`
		std::uint8_t alignAmount_;
		/// Current binding index for the uniform block. Negative if not bound.
		GLint bindingIndex_;
		char name_[MaxNameLength];
	};
}
