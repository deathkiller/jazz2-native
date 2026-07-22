#include "GLBufferObject.h"
#include "GLDebug.h"
#include "../../../../Main.h"
#include "../../../tracy_opengl.h"

namespace nCine::RHI::GL
{
	GLHashMap<GLBufferObjectMappingFunc::Size, GLBufferObjectMappingFunc> GLBufferObject::boundBuffers_;
	GLuint GLBufferObject::boundIndexBase_[MaxIndexBufferRange];
	GLBufferObject::BufferRange GLBufferObject::boundBufferRange_[MaxIndexBufferRange];

	GLBufferObject::GLBufferObject(GLenum target)
		: glHandle_(0), target_(target), size_(0), mapped_(false)
	{
		glGenBuffers(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	GLBufferObject::~GLBufferObject()
	{
		if (boundBuffers_[target_] == glHandle_)
			Unbind();

		// Scrub the indexed-binding caches, or a new buffer recycling this GL handle would be
		// considered already bound and the actual glBindBufferBase()/glBindBufferRange() skipped
		for (std::int32_t i = 0; i < MaxIndexBufferRange; i++) {
			if (boundIndexBase_[i] == glHandle_) {
				boundIndexBase_[i] = 0;
			}
			if (boundBufferRange_[i].glHandle == glHandle_) {
				boundBufferRange_[i].glHandle = 0;
				boundBufferRange_[i].offset = 0;
				boundBufferRange_[i].ptrsize = 0;
			}
		}

		glDeleteBuffers(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	bool GLBufferObject::Bind() const
	{
		if (boundBuffers_[target_] != glHandle_) {
			glBindBuffer(target_, glHandle_);
			GL_LOG_ERRORS();
			boundBuffers_[target_] = glHandle_;
			return true;
		}
		return false;
	}

	bool GLBufferObject::Unbind() const
	{
		if (boundBuffers_[target_] != 0) {
			glBindBuffer(target_, 0);
			GL_LOG_ERRORS();
			boundBuffers_[target_] = 0;
			return true;
		}
		return false;
	}

	void GLBufferObject::BufferData(GLsizeiptr size, const GLvoid* data, GLenum usage)
	{
		TracyGpuZone("glBufferData");
		Bind();
		glBufferData(target_, size, data, usage);
		GL_LOG_ERRORS();
		size_ = size;
	}

	void GLBufferObject::BufferSubData(GLintptr offset, GLsizeiptr size, const GLvoid* data)
	{
		TracyGpuZone("glBufferSubData");
		Bind();
		glBufferSubData(target_, offset, size, data);
		GL_LOG_ERRORS();
	}

#if !defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM))
	void GLBufferObject::BufferStorage(GLsizeiptr size, const GLvoid* data, GLbitfield flags)
	{
		TracyGpuZone("glBufferStorage");
		Bind();
		glBufferStorage(target_, size, data, flags);
		GL_LOG_ERRORS();
		size_ = size;
	}
#endif

	void GLBufferObject::BindBufferBase(GLuint index)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// Indexed uniform-buffer bindings (glBindBufferBase) are ES 3.0; nothing binds a UBO on the OpenGL|ES 2.0
		// profile - the block members are pushed as loose uniforms and CommitUniformBlocks() is a no-op
		static_cast<void>(index);
#else
		DEATH_ASSERT(target_ == GL_UNIFORM_BUFFER);
		DEATH_ASSERT(index < MaxIndexBufferRange);

		if (index >= MaxIndexBufferRange) {
			glBindBufferBase(target_, index, glHandle_);
		} else if (boundIndexBase_[index] != glHandle_) {
			boundBufferRange_[index].glHandle = -1;
			boundBufferRange_[index].offset = 0;
			boundBufferRange_[index].ptrsize = 0;
			boundIndexBase_[index] = glHandle_;
			glBindBufferBase(target_, index, glHandle_);
		}
		GL_LOG_ERRORS();
#endif
	}

	void GLBufferObject::BindBufferRange(GLuint index, GLintptr offset, GLsizei ptrsize)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// glBindBufferRange is ES 3.0; see BindBufferBase() above - no UBO is ever bound on this profile
		static_cast<void>(index);
		static_cast<void>(offset);
		static_cast<void>(ptrsize);
#else
		DEATH_ASSERT(target_ == GL_UNIFORM_BUFFER);
		DEATH_ASSERT(index < MaxIndexBufferRange);

		if (index >= MaxIndexBufferRange) {
			glBindBufferRange(target_, index, glHandle_, offset, ptrsize);
		} else if (boundBufferRange_[index].glHandle != glHandle_ ||
				 boundBufferRange_[index].offset != offset ||
				 boundBufferRange_[index].ptrsize != ptrsize) {
			boundIndexBase_[index] = -1;
			boundBufferRange_[index].glHandle = glHandle_;
			boundBufferRange_[index].offset = offset;
			boundBufferRange_[index].ptrsize = ptrsize;
			glBindBufferRange(target_, index, glHandle_, offset, ptrsize);
		}
		GL_LOG_ERRORS();
#endif
	}

	void* GLBufferObject::MapBufferRange(GLintptr offset, GLsizeiptr length, GLbitfield access)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// Buffer mapping (glMapBufferRange) is ES 3.0; the ES2 profile forces useBufferMapping=false and streams
		// via glBufferSubData instead, so this is never reached
		static_cast<void>(offset);
		static_cast<void>(length);
		static_cast<void>(access);
		return nullptr;
#else
		FATAL_ASSERT(mapped_ == false);
		mapped_ = true;
		Bind();
		void* result = glMapBufferRange(target_, offset, length, access);
		GL_LOG_ERRORS();
		return result;
#endif
	}

	void GLBufferObject::FlushMappedBufferRange(GLintptr offset, GLsizeiptr length)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// glFlushMappedBufferRange is ES 3.0; unreached on this profile (see MapBufferRange above)
		static_cast<void>(offset);
		static_cast<void>(length);
#else
		FATAL_ASSERT(mapped_ == true);
		Bind();
		glFlushMappedBufferRange(target_, offset, length);
		GL_LOG_ERRORS();
#endif
	}

	GLboolean GLBufferObject::Unmap()
	{
#if defined(RHI_GL_PROFILE_ES2)
		// glUnmapBuffer's ES 3.0 signature is unreached on this profile (see MapBufferRange above)
		return GL_TRUE;
#else
		FATAL_ASSERT(mapped_ == true);
		mapped_ = false;
		Bind();
		GLboolean result = glUnmapBuffer(target_);
		GL_LOG_ERRORS();
		return result;
#endif
	}

#if !defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_2)
	void GLBufferObject::TexBuffer(GLenum internalformat)
	{
		FATAL_ASSERT(target_ == GL_TEXTURE_BUFFER);
		glTexBuffer(GL_TEXTURE_BUFFER, internalformat, glHandle_);
		GL_LOG_ERRORS();
	}
#endif

	void GLBufferObject::SetObjectLabel(StringView label)
	{
		GLDebug::SetObjectLabel(GLDebug::LabelTypes::Buffer, glHandle_, label);
	}

	bool GLBufferObject::BindHandle(GLenum target, GLuint glHandle)
	{
		if (boundBuffers_[target] != glHandle) {
			glBindBuffer(target, glHandle);
			GL_LOG_ERRORS();
			boundBuffers_[target] = glHandle;
			return true;
		}
		return false;
	}

	GLuint GLBufferObject::GetBoundHandle(GLenum target)
	{
		return boundBuffers_[target];
	}
}
