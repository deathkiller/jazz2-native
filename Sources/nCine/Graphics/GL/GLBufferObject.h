#pragma once

#include "GLHashMap.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	/// Handles OpenGL buffer objects of different kinds
	class GLBufferObject
	{
		friend class RenderVaoPool;

	public:
		explicit GLBufferObject(GLenum target);
		~GLBufferObject();

		inline GLuint GetGLHandle() const {
			return glHandle_;
		}
		inline GLenum GetTarget() const {
			return target_;
		}
		inline GLsizeiptr GetSize() const {
			return size_;
		}

		bool Bind() const;
		bool Unbind() const;

		void BufferData(GLsizeiptr size, const GLvoid* data, GLenum usage);
		void BufferSubData(GLintptr offset, GLsizeiptr size, const GLvoid* data);
#if !defined(WITH_OPENGLES) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM))
		void BufferStorage(GLsizeiptr size, const GLvoid* data, GLbitfield flags);
#endif

		void BindBufferBase(GLuint index);
		void BindBufferRange(GLuint index, GLintptr offset, GLsizei ptrsize);
		void* MapBufferRange(GLintptr offset, GLsizeiptr length, GLbitfield access);
		void FlushMappedBufferRange(GLintptr offset, GLsizeiptr length);
		GLboolean Unmap();
#if !defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_2)
		void TexBuffer(GLenum internalformat);
#endif

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
		/// Current bound index for buffer base. Negative if not bound.
		static GLuint boundIndexBase_[MaxIndexBufferRange];
		/// Current range and offset for buffer range index
		static BufferRange boundBufferRange_[MaxIndexBufferRange];

		/// Deleted copy constructor
		GLBufferObject(const GLBufferObject&) = delete;
		/// Deleted assignment operator
		GLBufferObject& operator=(const GLBufferObject&) = delete;

		inline static void SetBoundHandle(GLenum target, GLuint glHandle) {
			boundBuffers_[target] = glHandle;
		}
		static bool BindHandle(GLenum target, GLuint glHandle);
	};

}
