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

	/// Caches a uniform block buffer and then updates it in the shader
	class GLUniformBlockCache
	{
	public:
		GLUniformBlockCache();
		explicit GLUniformBlockCache(GLUniformBlock* uniformBlock);

		inline const GLUniformBlock* uniformBlock() const {
			return uniformBlock_;
		}
		/// Wrapper around `GLUniformBlock::index()`
		GLuint index() const;
		/// Wrapper around `GLUniformBlock::bindingIndex()`
		GLuint bindingIndex() const;
		/// Wrapper around `GLUniformBlock::size()`
		GLint size() const;
		/// Wrapper around `GLUniformBlock::alignAmount()`
		std::uint8_t alignAmount() const;

		inline GLubyte* dataPointer() {
			return dataPointer_;
		}
		inline const GLubyte* dataPointer() const {
			return dataPointer_;
		}
		void setDataPointer(GLubyte* dataPointer);

		inline GLint usedSize() const {
			return usedSize_;
		}
		void setUsedSize(GLint usedSize);

		bool copyData(std::uint32_t destIndex, const GLubyte* src, std::uint32_t numBytes);
		inline bool copyData(const GLubyte* src) {
			return copyData(0, src, usedSize_);
		}

		GLUniformCache* uniform(StringView name);
		/// Wrapper around `GLUniformBlock::setBlockBinding()`
		void setBlockBinding(GLuint blockBinding);

	private:
		GLUniformBlock* uniformBlock_;
		GLubyte* dataPointer_;
		/// Keeps tracks of how much of the cache needs to be uploaded to the UBO
		GLint usedSize_;

		static const std::uint32_t UniformHashSize = 8;
		StaticHashMap<String, GLUniformCache, UniformHashSize> uniformCaches_;
	};

}
