#include "GLFramebuffer.h"
#include "GLRenderbuffer.h"
#include "GLTexture.h"
#include "GLDebug.h"
#include "../../../../Main.h"

namespace nCine::RHI::GL
{
	std::uint32_t GLFramebuffer::_readBoundBuffer = 0;
	std::uint32_t GLFramebuffer::_drawBoundBuffer = 0;
	GLuint GLFramebuffer::_defaultHandle = 0;

	GLFramebuffer::GLFramebuffer()
		: _glHandle(0), _numDrawBuffers(0)
	{
		glGenFramebuffers(1, &_glHandle);
		GL_LOG_ERRORS();
	}

	GLFramebuffer::~GLFramebuffer()
	{
		if (_readBoundBuffer == _glHandle) {
			Unbind(GL_READ_FRAMEBUFFER);
		}
		if (_drawBoundBuffer == _glHandle) {
			Unbind(GL_DRAW_FRAMEBUFFER);
		}
		glDeleteFramebuffers(1, &_glHandle);
		GL_LOG_ERRORS();
	}

	bool GLFramebuffer::Bind() const
	{
		return Bind(GL_FRAMEBUFFER);
	}

	bool GLFramebuffer::Unbind()
	{
		return Unbind(GL_FRAMEBUFFER);
	}

	bool GLFramebuffer::Bind(GLenum target) const
	{
		return BindHandle(target, _glHandle);
	}

	bool GLFramebuffer::Unbind(GLenum target)
	{
		return BindHandle(target, _defaultHandle);
	}

	bool GLFramebuffer::DrawBuffers(std::uint32_t numDrawBuffers)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// glDrawBuffers() is ES 3.0 and MRT does not exist on ES2 - a framebuffer always renders to its single
		// GL_COLOR_ATTACHMENT0, which is exactly what a draw-buffer count of 0/1 selects, so those are no-ops
		DEATH_ASSERT(numDrawBuffers <= 1, "Multiple render targets are not supported on OpenGL|ES 2.0", false);
		_numDrawBuffers = numDrawBuffers;
		return false;
#else
		static const GLenum drawBuffers[MaxDrawbuffers] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3,
															GL_COLOR_ATTACHMENT4, GL_COLOR_ATTACHMENT5, GL_COLOR_ATTACHMENT6, GL_COLOR_ATTACHMENT7 };
		if (numDrawBuffers < MaxDrawbuffers && _numDrawBuffers != numDrawBuffers) {
			glDrawBuffers(numDrawBuffers, drawBuffers);
			GL_LOG_ERRORS();
			_numDrawBuffers = numDrawBuffers;
			return true;
		}
		return false;
#endif
	}

	bool GLFramebuffer::AttachRenderbuffer(const char* label, GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment)
	{
		if (_attachedRenderbuffers.size() >= MaxRenderbuffers - 1) {
			return false;
		}
		for (std::uint32_t i = 0; i < _attachedRenderbuffers.size(); i++) {
			if (_attachedRenderbuffers[i]->GetAttachment() == attachment) {
				return false;
			}
		}

		std::unique_ptr<GLRenderbuffer>& buffer = _attachedRenderbuffers.emplace_back(std::make_unique<GLRenderbuffer>(internalFormat, width, height));
		buffer->SetObjectLabel(label);
		buffer->SetAttachment(attachment);

		Bind(GL_FRAMEBUFFER);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, buffer->_glHandle);
		GL_LOG_ERRORS();
		return true;
	}

	bool GLFramebuffer::AttachRenderbuffer(GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment)
	{
		return AttachRenderbuffer(nullptr, internalFormat, width, height, attachment);
	}

	bool GLFramebuffer::DetachRenderbuffer(GLenum attachment)
	{
		for (std::uint32_t i = 0; i < _attachedRenderbuffers.size(); i++) {
			if (_attachedRenderbuffers[i]->GetAttachment() == attachment) {
				Bind(GL_FRAMEBUFFER);
				glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, 0);
				GL_LOG_ERRORS();
				_attachedRenderbuffers.eraseUnordered(i);
				return true;
			}
		}
		return false;
	}

	void GLFramebuffer::AttachTexture(GLTexture& texture, GLenum attachment)
	{
		Bind(GL_FRAMEBUFFER);
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, texture._target, texture._glHandle, 0);
		GL_LOG_ERRORS();
	}

	void GLFramebuffer::DetachTexture(GLenum attachment)
	{
		Bind(GL_FRAMEBUFFER);
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, 0, 0);
		GL_LOG_ERRORS();
	}

#if !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM))
	void GLFramebuffer::Invalidate(GLsizei numAttachments, const GLenum* attachments)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// glInvalidateFramebuffer() is ES 3.0 (ES2 would need EXT_discard_framebuffer, not assumed) and
		// invalidation is purely a bandwidth optimization - skipping it is always correct
		static_cast<void>(numAttachments);
		static_cast<void>(attachments);
#else
		Bind(GL_FRAMEBUFFER);
		glInvalidateFramebuffer(GL_FRAMEBUFFER, numAttachments, attachments);
		GL_LOG_ERRORS();
#endif
	}
#endif

	bool GLFramebuffer::IsStatusComplete()
	{
		Bind(GL_FRAMEBUFFER);
		const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		Unbind(GL_FRAMEBUFFER);

		return (status == GL_FRAMEBUFFER_COMPLETE);
	}

	void GLFramebuffer::SetObjectLabel(StringView label)
	{
		GLDebug::SetObjectLabel(GLDebug::LabelTypes::FrameBuffer, _glHandle, label);
	}

	bool GLFramebuffer::BindHandle(GLenum target, GLuint glHandle)
	{
		FATAL_ASSERT(target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER);

#if defined(RHI_GL_PROFILE_ES2)
		// ES2 only has the combined GL_FRAMEBUFFER target (separate READ/DRAW bind points are ES 3.0);
		// remap every request so callers like GLRenderTarget::BindDraw() and the destructor stay portable
		target = GL_FRAMEBUFFER;
#endif
		if (target == GL_FRAMEBUFFER && (_readBoundBuffer != glHandle || _drawBoundBuffer != glHandle)) {
			glBindFramebuffer(target, glHandle);
			GL_LOG_ERRORS();
			_readBoundBuffer = glHandle;
			_drawBoundBuffer = glHandle;
			return true;
		} else if (target == GL_READ_FRAMEBUFFER && _readBoundBuffer != glHandle) {
			glBindFramebuffer(target, glHandle);
			GL_LOG_ERRORS();
			_readBoundBuffer = glHandle;
			return true;
		} else if (target == GL_DRAW_FRAMEBUFFER && _drawBoundBuffer != glHandle) {
			glBindFramebuffer(target, glHandle);
			GL_LOG_ERRORS();
			_drawBoundBuffer = glHandle;
			return true;
		}
		return false;
	}

	GLuint GLFramebuffer::GetBoundHandle(GLenum target)
	{
		FATAL_ASSERT(target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER);

		if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER)
			return _readBoundBuffer;
		else
			return _drawBoundBuffer;
	}

	void GLFramebuffer::SetBoundHandle(GLenum target, GLuint glHandle)
	{
		FATAL_ASSERT(target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER);

		if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER)
			_readBoundBuffer = glHandle;

		if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER)
			_drawBoundBuffer = glHandle;
	}
}
