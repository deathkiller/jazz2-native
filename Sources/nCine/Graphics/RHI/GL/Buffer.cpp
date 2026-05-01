#if defined(WITH_RHI_GL)
#include "Buffer.h"
#include "Debug.h"
#include "../../../../Main.h"
#include "../../../tracy_opengl.h"

namespace nCine::RHI
{
	HashMap<BufferMappingFunc::Size, BufferMappingFunc> Buffer::boundBuffers_;
	GLuint Buffer::boundIndexBase_[MaxIndexBufferRange];
	Buffer::BufferRange Buffer::boundBufferRange_[MaxIndexBufferRange];

	Buffer::Buffer(GLenum target)
		: glHandle_(0), target_(target), size_(0), mapped_(false)
	{
		for (std::int32_t i = 0; i < MaxIndexBufferRange; i++)
			boundIndexBase_[i] = 0;

		glGenBuffers(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	Buffer::~Buffer()
	{
		if (boundBuffers_[target_] == glHandle_)
			Unbind();

		glDeleteBuffers(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	bool Buffer::Bind() const
	{
		if (boundBuffers_[target_] != glHandle_) {
			glBindBuffer(target_, glHandle_);
			GL_LOG_ERRORS();
			boundBuffers_[target_] = glHandle_;
			return true;
		}
		return false;
	}

	bool Buffer::Unbind() const
	{
		if (boundBuffers_[target_] != 0) {
			glBindBuffer(target_, 0);
			GL_LOG_ERRORS();
			boundBuffers_[target_] = 0;
			return true;
		}
		return false;
	}

	void Buffer::BufferData(GLsizeiptr size, const GLvoid* data, GLenum usage)
	{
		TracyGpuZone("glBufferData");
		Bind();
		glBufferData(target_, size, data, usage);
		GL_LOG_ERRORS();
		size_ = size;
	}

	void Buffer::BufferSubData(GLintptr offset, GLsizeiptr size, const GLvoid* data)
	{
		TracyGpuZone("glBufferSubData");
		Bind();
		glBufferSubData(target_, offset, size, data);
		GL_LOG_ERRORS();
	}

#if !defined(WITH_OPENGLES) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM))
	void Buffer::BufferStorage(GLsizeiptr size, const GLvoid* data, GLbitfield flags)
	{
		TracyGpuZone("glBufferStorage");
		Bind();
		glBufferStorage(target_, size, data, flags);
		GL_LOG_ERRORS();
		size_ = size;
	}
#endif

	void Buffer::BindBufferBase(GLuint index)
	{
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
	}

	void Buffer::BindBufferRange(GLuint index, GLintptr offset, GLsizei ptrsize)
	{
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
	}

	void* Buffer::MapBufferRange(GLintptr offset, GLsizeiptr length, GLbitfield access)
	{
		FATAL_ASSERT(mapped_ == false);
		mapped_ = true;
		Bind();
		void* result = glMapBufferRange(target_, offset, length, access);
		GL_LOG_ERRORS();
		return result;
	}

	void Buffer::FlushMappedBufferRange(GLintptr offset, GLsizeiptr length)
	{
		FATAL_ASSERT(mapped_ == true);
		Bind();
		glFlushMappedBufferRange(target_, offset, length);
		GL_LOG_ERRORS();
	}

	GLboolean Buffer::Unmap()
	{
		FATAL_ASSERT(mapped_ == true);
		mapped_ = false;
		Bind();
		GLboolean result = glUnmapBuffer(target_);
		GL_LOG_ERRORS();
		return result;
	}

#if !defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_2)
	void Buffer::TexBuffer(GLenum internalformat)
	{
		FATAL_ASSERT(target_ == GL_TEXTURE_BUFFER);
		glTexBuffer(GL_TEXTURE_BUFFER, internalformat, glHandle_);
		GL_LOG_ERRORS();
	}
#endif

	void Buffer::SetObjectLabel(StringView label)
	{
		Debug::SetObjectLabel(Debug::LabelTypes::Buffer, glHandle_, label);
	}

	bool Buffer::BindHandle(GLenum target, GLuint glHandle)
	{
		if (boundBuffers_[target] != glHandle) {
			glBindBuffer(target, glHandle);
			GL_LOG_ERRORS();
			boundBuffers_[target] = glHandle;
			return true;
		}
		return false;
	}

	GLuint Buffer::GetBoundHandle(GLenum target)
	{
		return boundBuffers_[target];
	}
}

#endif // defined(WITH_RHI_GL)
