#pragma once

#include "GLHashMap.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Wraps an OpenGL buffer object
		
		Manages the lifetime of a single OpenGL buffer object bound to a given target (e.g., a VBO, IBO
		or UBO). Binding is cached per target so that redundant `glBindBuffer()` calls are skipped, and
		indexed binding state (base/range) is tracked for uniform buffers. Provides data upload, immutable
		storage allocation and persistent mapping.
	*/
	class GLBufferObject
	{
		friend class RenderVaoPool;

	public:
		explicit GLBufferObject(GLenum target);
		~GLBufferObject();

		/** @brief Returns the OpenGL handle of the buffer object */
		inline GLuint GetGLHandle() const {
			return glHandle_;
		}
		/** @brief Returns the target this buffer is bound to (e.g., `GL_ARRAY_BUFFER`) */
		inline GLenum GetTarget() const {
			return target_;
		}
		/** @brief Returns the size in bytes of the data store, as last set by @ref BufferData() or @ref BufferStorage() */
		inline GLsizeiptr GetSize() const {
			return size_;
		}

		/**
		 * @brief Binds the buffer to its target
		 *
		 * @return `true` if a `glBindBuffer()` call was issued, `false` if it was already bound
		 */
		bool Bind() const;
		/**
		 * @brief Unbinds any buffer from this object's target
		 *
		 * @return `true` if a `glBindBuffer()` call was issued, `false` if nothing was bound
		 */
		bool Unbind() const;

		/** @brief Creates and initializes the data store with the given size, optional data and usage hint */
		void BufferData(GLsizeiptr size, const GLvoid* data, GLenum usage);
		/** @brief Updates a subset of the data store starting at the given byte offset */
		void BufferSubData(GLintptr offset, GLsizeiptr size, const GLvoid* data);
#if !defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM))
		/** @brief Allocates an immutable data store with the given size, optional data and storage flags */
		void BufferStorage(GLsizeiptr size, const GLvoid* data, GLbitfield flags);
#endif

		/** @brief Binds the whole buffer to a binding point index of the (uniform) buffer target */
		void BindBufferBase(GLuint index);
		/** @brief Binds a byte range of the buffer to a binding point index of the (uniform) buffer target */
		void BindBufferRange(GLuint index, GLintptr offset, GLsizei ptrsize);
		/** @brief Maps a byte range of the buffer into client memory and returns the pointer */
		void* MapBufferRange(GLintptr offset, GLsizeiptr length, GLbitfield access);
		/** @brief Flushes a byte range of the currently mapped buffer */
		void FlushMappedBufferRange(GLintptr offset, GLsizeiptr length);
		/**
		 * @brief Unmaps the previously mapped buffer
		 *
		 * @return `GL_FALSE` if the buffer contents became corrupt while mapped
		 */
		GLboolean Unmap();
#if !defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_2)
		/** @brief Attaches the buffer storage to the active texture as a texture buffer with the given internal format */
		void TexBuffer(GLenum internalformat);
#endif

		/** @brief Sets an OpenGL object label for the buffer, for debugging */
		void SetObjectLabel(StringView label);

	private:
		static class GLHashMap<GLBufferObjectMappingFunc::Size, GLBufferObjectMappingFunc> boundBuffers_;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct BufferRange
		{
			BufferRange()
				: glHandle(0), offset(0), ptrsize(0) {}

			GLuint glHandle;
			GLintptr offset;
			GLsizei ptrsize;
		};
#endif

		GLuint glHandle_;
		GLenum target_;
		GLsizeiptr size_;
		bool mapped_;

		static const std::int32_t MaxIndexBufferRange = 128;
		/** @brief Handle bound at each base binding point index, or 0 if none */
		static GLuint boundIndexBase_[MaxIndexBufferRange];
		/** @brief Handle, offset and size bound at each ranged binding point index */
		static BufferRange boundBufferRange_[MaxIndexBufferRange];

		/** @brief Deleted copy constructor */
		GLBufferObject(const GLBufferObject&) = delete;
		/** @brief Deleted assignment operator */
		GLBufferObject& operator=(const GLBufferObject&) = delete;

		static bool BindHandle(GLenum target, GLuint glHandle);

		static GLuint GetBoundHandle(GLenum target);
		inline static void SetBoundHandle(GLenum target, GLuint glHandle) {
			boundBuffers_[target] = glHandle;
		}
	};

}
