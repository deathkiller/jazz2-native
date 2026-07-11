#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "GLUniformCache.h"
#include "../../../Base/StaticHashMap.h"

namespace nCine::RhiGL
{
	class GLUniformBlock;
	class GLBufferObject;

	/**
		@brief Caches the contents of a uniform block and uploads it through a buffer object
		
		Holds a host-side data buffer mirroring a @ref GLUniformBlock and manages a
		@ref GLUniformCache for each of the block's member uniforms, pointing each one into the
		appropriate offset of the shared buffer. The contents are uploaded to the GL as a single
		uniform buffer object (UBO).
	*/
	class GLUniformBlockCache
	{
	public:
		/** @brief Creates an empty cache not associated with any uniform block */
		GLUniformBlockCache();
		/** @brief Creates a cache for the specified uniform block */
		explicit GLUniformBlockCache(GLUniformBlock* uniformBlock);

		/** @brief Returns the uniform block described by this cache */
		inline const GLUniformBlock* uniformBlock() const {
			return uniformBlock_;
		}
		/** @brief Wrapper around @ref GLUniformBlock::GetIndex() */
		GLuint GetIndex() const;
		/** @brief Wrapper around @ref GLUniformBlock::GetBindingIndex() */
		GLuint GetBindingIndex() const;
		/** @brief Wrapper around @ref GLUniformBlock::GetSize() */
		GLint GetSize() const;
		/** @brief Wrapper around @ref GLUniformBlock::GetAlignAmount() */
		std::uint8_t GetAlignAmount() const;

		/** @brief Returns the host-side data buffer mirroring the block */
		inline GLubyte* GetDataPointer() {
			return dataPointer_;
		}
		/** @brief Returns the host-side data buffer mirroring the block */
		inline const GLubyte* GetDataPointer() const {
			return dataPointer_;
		}
		/** @brief Sets the host-side data buffer and repoints each member uniform cache into it */
		void SetDataPointer(GLubyte* dataPointer);

		/** @brief Returns how many bytes of the cache are uploaded to the UBO */
		inline GLint usedSize() const {
			return usedSize_;
		}
		/** @brief Sets how many bytes of the cache are uploaded to the UBO */
		void SetUsedSize(GLint usedSize);

		/** @brief Copies a range of bytes into the data buffer and returns whether it succeeded */
		bool CopyData(std::uint32_t destIndex, const GLubyte* src, std::uint32_t numBytes);
		/** @brief Copies the whole used size from the source into the data buffer */
		inline bool CopyData(const GLubyte* src) {
			return CopyData(0, src, usedSize_);
		}

		/** @brief Returns the member uniform cache with the specified name, or `nullptr` if not found */
		GLUniformCache* GetUniform(StringView name);
		/** @brief Wrapper around @ref GLUniformBlock::SetBlockBinding() */
		void SetBlockBinding(GLuint blockBinding);

	private:
		GLUniformBlock* uniformBlock_;
		GLubyte* dataPointer_;
		// Keeps tracks of how much of the cache needs to be uploaded to the UBO
		GLint usedSize_;

		static const std::uint32_t UniformHashSize = 8;
		StaticHashMap<String, GLUniformCache, UniformHashSize> uniformCaches_;
	};

}
