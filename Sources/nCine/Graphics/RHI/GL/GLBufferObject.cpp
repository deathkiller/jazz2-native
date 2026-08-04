#include "GLBufferObject.h"
#include "GLDebug.h"
#include "../../../../Main.h"
#include "../../../tracy_opengl.h"

namespace nCine::RHI::GL
{
	GLHashMap<GLBufferObjectMappingFunc::Size, GLBufferObjectMappingFunc> GLBufferObject::_boundBuffers;
	GLuint GLBufferObject::_boundIndexBase[MaxIndexBufferRange];
	GLBufferObject::BufferRange GLBufferObject::_boundBufferRange[MaxIndexBufferRange];

	GLBufferObject::GLBufferObject(GLenum target)
		: _glHandle(0), _target(target), _size(0), _mapped(false)
	{
		glGenBuffers(1, &_glHandle);
		GL_LOG_ERRORS();
	}

	GLBufferObject::~GLBufferObject()
	{
		if (_boundBuffers[_target] == _glHandle)
			Unbind();

		// Scrub the indexed-binding caches, or a new buffer recycling this GL handle would be
		// considered already bound and the actual glBindBufferBase()/glBindBufferRange() skipped
		for (std::int32_t i = 0; i < MaxIndexBufferRange; i++) {
			if (_boundIndexBase[i] == _glHandle) {
				_boundIndexBase[i] = 0;
			}
			if (_boundBufferRange[i].glHandle == _glHandle) {
				_boundBufferRange[i].glHandle = 0;
				_boundBufferRange[i].offset = 0;
				_boundBufferRange[i].ptrsize = 0;
			}
		}

		glDeleteBuffers(1, &_glHandle);
		GL_LOG_ERRORS();
	}

	bool GLBufferObject::Bind() const
	{
		if (_boundBuffers[_target] != _glHandle) {
			glBindBuffer(_target, _glHandle);
			GL_LOG_ERRORS();
			_boundBuffers[_target] = _glHandle;
			return true;
		}
		return false;
	}

	bool GLBufferObject::Unbind() const
	{
		if (_boundBuffers[_target] != 0) {
			glBindBuffer(_target, 0);
			GL_LOG_ERRORS();
			_boundBuffers[_target] = 0;
			return true;
		}
		return false;
	}

	void GLBufferObject::BufferData(GLsizeiptr size, const GLvoid* data, GLenum usage)
	{
		TracyGpuZone("glBufferData");
		Bind();
		glBufferData(_target, size, data, usage);
		GL_LOG_ERRORS();
		_size = size;
	}

	void GLBufferObject::BufferSubData(GLintptr offset, GLsizeiptr size, const GLvoid* data)
	{
		TracyGpuZone("glBufferSubData");
		Bind();
		glBufferSubData(_target, offset, size, data);
		GL_LOG_ERRORS();
	}

#if defined(RHI_GL_PROFILE_CORE) && !defined(DEATH_TARGET_EMSCRIPTEN) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM))
	void GLBufferObject::BufferStorage(GLsizeiptr size, const GLvoid* data, GLbitfield flags)
	{
		TracyGpuZone("glBufferStorage");
		Bind();
		glBufferStorage(_target, size, data, flags);
		GL_LOG_ERRORS();
		_size = size;
	}
#endif

	void GLBufferObject::BindBufferBase(GLuint index)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// Indexed uniform-buffer bindings (glBindBufferBase) are ES 3.0; nothing binds a UBO on the OpenGL|ES 2.0
		// profile - the block members are pushed as loose uniforms and CommitUniformBlocks() is a no-op
		static_cast<void>(index);
#else
		DEATH_ASSERT(_target == GL_UNIFORM_BUFFER);
		DEATH_ASSERT(index < MaxIndexBufferRange);

		if (index >= MaxIndexBufferRange) {
			glBindBufferBase(_target, index, _glHandle);
		} else if (_boundIndexBase[index] != _glHandle) {
			_boundBufferRange[index].glHandle = -1;
			_boundBufferRange[index].offset = 0;
			_boundBufferRange[index].ptrsize = 0;
			_boundIndexBase[index] = _glHandle;
			glBindBufferBase(_target, index, _glHandle);
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
		DEATH_ASSERT(_target == GL_UNIFORM_BUFFER);
		DEATH_ASSERT(index < MaxIndexBufferRange);

		if (index >= MaxIndexBufferRange) {
			glBindBufferRange(_target, index, _glHandle, offset, ptrsize);
		} else if (_boundBufferRange[index].glHandle != _glHandle ||
				 _boundBufferRange[index].offset != offset ||
				 _boundBufferRange[index].ptrsize != ptrsize) {
			_boundIndexBase[index] = -1;
			_boundBufferRange[index].glHandle = _glHandle;
			_boundBufferRange[index].offset = offset;
			_boundBufferRange[index].ptrsize = ptrsize;
			glBindBufferRange(_target, index, _glHandle, offset, ptrsize);
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
		FATAL_ASSERT(_mapped == false);
		_mapped = true;
		Bind();
		void* result = glMapBufferRange(_target, offset, length, access);
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
		FATAL_ASSERT(_mapped == true);
		Bind();
		glFlushMappedBufferRange(_target, offset, length);
		GL_LOG_ERRORS();
#endif
	}

	GLboolean GLBufferObject::Unmap()
	{
#if defined(RHI_GL_PROFILE_ES2)
		// glUnmapBuffer's ES 3.0 signature is unreached on this profile (see MapBufferRange above)
		return GL_TRUE;
#else
		FATAL_ASSERT(_mapped == true);
		_mapped = false;
		Bind();
		GLboolean result = glUnmapBuffer(_target);
		GL_LOG_ERRORS();
		return result;
#endif
	}

#if defined(RHI_GL_PROFILE_CORE) || GL_ES_VERSION_3_2
	void GLBufferObject::TexBuffer(GLenum internalformat)
	{
		FATAL_ASSERT(_target == GL_TEXTURE_BUFFER);
		glTexBuffer(GL_TEXTURE_BUFFER, internalformat, _glHandle);
		GL_LOG_ERRORS();
	}
#endif

	void GLBufferObject::SetObjectLabel(StringView label)
	{
		GLDebug::SetObjectLabel(GLDebug::LabelTypes::Buffer, _glHandle, label);
	}

	bool GLBufferObject::BindHandle(GLenum target, GLuint glHandle)
	{
		if (_boundBuffers[target] != glHandle) {
			glBindBuffer(target, glHandle);
			GL_LOG_ERRORS();
			_boundBuffers[target] = glHandle;
			return true;
		}
		return false;
	}

	GLuint GLBufferObject::GetBoundHandle(GLenum target)
	{
		return _boundBuffers[target];
	}
}
