#pragma once

#if defined(WITH_RHI_GL)

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "UniformCache.h"
#include "../../../Base/StaticHashMap.h"

namespace nCine::RHI
{
	class UniformBlock;
	class Buffer;

	/// Caches a uniform block buffer and then updates it in the shader
	class UniformBlockCache
	{
	public:
		UniformBlockCache();
		explicit UniformBlockCache(UniformBlock* uniformBlock);

		inline const UniformBlock* uniformBlock() const {
			return uniformBlock_;
		}
		/// Wrapper around `UniformBlock::index()`
		GLuint GetIndex() const;
		/// Wrapper around `UniformBlock::bindingIndex()`
		GLuint GetBindingIndex() const;
		/// Wrapper around `UniformBlock::size()`
		GLint GetSize() const;
		/// Wrapper around `UniformBlock::alignAmount()`
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

		UniformCache* GetUniform(StringView name);
		/// Wrapper around `UniformBlock::SetBlockBinding()`
		void SetBlockBinding(GLuint blockBinding);

	private:
		UniformBlock* uniformBlock_;
		GLubyte* dataPointer_;
		/// Keeps tracks of how much of the cache needs to be uploaded to the UBO
		GLint usedSize_;

		static const std::uint32_t UniformHashSize = 8;
		StaticHashMap<String, UniformCache, UniformHashSize> uniformCaches_;
	};

}

#endif // defined(WITH_RHI_GL)
