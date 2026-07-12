#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "GLUniform.h"
#include "../../../Base/StaticHashMapIterator.h"

namespace nCine::RhiGL
{
	class GLShaderProgram;

	/**
		@brief Stores information about an active uniform block
		
		Holds the metadata queried for a single active uniform block of a linked shader program,
		such as its index, byte size (padded to the uniform buffer offset alignment), current
		binding index and the @ref GLUniform members it contains. Member uniforms are discovered
		only when @ref DiscoverUniforms::ENABLED is requested.
	*/
	class GLUniformBlock
	{
		friend class GLUniformBlockCache;
		friend class GLShaderProgram;

	public:
		/** @brief Maximum length of a uniform block name, including the terminating null character */
		static constexpr std::uint32_t MaxNameLength = 48;

		/** @brief Whether the member uniforms of a block should be queried on construction */
		enum class DiscoverUniforms
		{
			ENABLED,
			DISABLED
		};

		/** @brief Creates an empty, unbound uniform block */
		GLUniformBlock();
		/** @brief Queries the active uniform block at the specified index, discovering members as requested */
		GLUniformBlock(GLuint program, GLuint blockIndex, DiscoverUniforms discover);
		/** @brief Queries the active uniform block at the specified index, discovering its member uniforms */
		GLUniformBlock(GLuint program, GLuint blockIndex);
		/** @brief Creates a uniform block from offline reflection data (name, queried index and unaligned data size), without member discovery */
		GLUniformBlock(GLuint program, const char* name, GLuint blockIndex, GLint dataSize);

		/** @brief Returns the active uniform block index within the program */
		inline GLuint GetIndex() const {
			return index_;
		}
		/** @brief Returns the current binding index of the block, or -1 if not bound */
		inline GLint GetBindingIndex() const {
			return bindingIndex_;
		}
		/** @brief Returns the block size in bytes, padded to the uniform buffer offset alignment */
		inline GLint GetSize() const {
			return size_;
		}
		/** @brief Returns the number of padding bytes added to the original size for offset alignment */
		inline std::uint8_t GetAlignAmount() const {
			return alignAmount_;
		}
		/** @brief Returns the uniform block name */
		inline const char* GetName() const {
			return name_;
		}

		/** @brief Returns the member uniform with the specified name, or `nullptr` if not found */
		inline GLUniform* GetUniform(const char* name) {
			return blockUniforms_.find(String::nullTerminatedView(name));
		}
		/** @brief Sets the binding index of the block, skipping the GL call if it is unchanged */
		void SetBlockBinding(GLuint blockBinding);

	private:
		// Max number of discoverable uniforms per block
		static const std::int32_t MaxNumBlockUniforms = 16;

		static const std::uint32_t BlockUniformHashSize = 8;
		StaticHashMap<String, GLUniform, BlockUniformHashSize> blockUniforms_;

		GLuint program_;
		GLuint index_;
		// Offset aligned size for `glBindBufferRange()` calls
		GLint size_;
		// Uniform buffer offset alignment added to `size_`
		std::uint8_t alignAmount_;
		// Current binding index for the uniform block. Negative if not bound.
		GLint bindingIndex_;
		char name_[MaxNameLength];
	};
}
