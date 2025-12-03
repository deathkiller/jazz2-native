#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

#include "GLUniformCache.h"
#include "../../Base/StaticHashMap.h"

namespace nCine
{
	class GLUniformBlock;
	class GLBufferObject;
	class GLShaderUniforms;

	/// Caches a uniform block buffer and then updates it in the shader
	class GLUniformBlockCache
	{
		friend class GLShaderUniformBlocks;

	public:
		GLUniformBlockCache();
		explicit GLUniformBlockCache(GLUniformBlock* uniformBlock);

		inline const GLUniformBlock* uniformBlock() const {
			return uniformBlock_;
		}
		/// Wrapper around `GLUniformBlock::index()`
		GLuint GetIndex() const;
		/// Wrapper around `GLUniformBlock::bindingIndex()`
		GLuint GetBindingIndex() const;
		/// Wrapper around `GLUniformBlock::size()`
		GLint GetSize() const;
		/// Wrapper around `GLUniformBlock::alignAmount()`
		std::uint8_t GetAlignAmount() const;

		inline GLubyte* GetDataPointer() {
			return dataPointer_;
		}
		inline const GLubyte* GetDataPointer() const {
			return dataPointer_;
		}
		void SetDataPointer(GLubyte* dataPointer);

		inline GLint usedSize() const {
			return usedSize_;
		}
		void SetUsedSize(GLint usedSize);

		bool CopyData(std::uint32_t destIndex, const GLubyte* src, std::uint32_t numBytes);
		inline bool CopyData(const GLubyte* src) {
			return CopyData(0, src, usedSize_);
		}

		GLUniformCache* GetUniform(StringView name);
		/// Wrapper around `GLUniformBlock::SetBlockBinding()`
		void SetBlockBinding(GLuint blockBinding);

	private:
		GLUniformBlock* uniformBlock_;
		GLubyte* dataPointer_;
		/// Keeps tracks of how much of the cache needs to be uploaded to the UBO
		GLint usedSize_;

		static const std::uint32_t UniformHashSize = 8;
		StaticHashMap<String, GLUniformCache, UniformHashSize> uniformCaches_;
	};

}
