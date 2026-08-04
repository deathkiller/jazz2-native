#include "GLVertexArrayObject.h"
#include "GLDebug.h"

#if defined(RHI_GL_PROFILE_ES2) && !defined(DEATH_TARGET_VITA)
// Vertex array objects are not ES 2.0 core - this profile relies on GL_OES_vertex_array_object, whose entry
// points carry the OES suffix on a true ES2 context. Being extension functions, they are not necessarily
// *exported* by the client library either (ANGLE exports them, Mesa's libGLESv2 does not), so they are
// resolved once through eglGetProcAddress() - the ES profiles always create their context through EGL. The
// extension is required by this engine's render loop, which binds all vertex state through VAOs. PS Vita's
// vitaGL is the exception: it exports the unsuffixed entry points directly, so nothing is remapped there.
#	include <EGL/egl.h>

namespace nCine::RHI::GL
{
	namespace
	{
		void (GL_APIENTRY* _glGenVertexArrays)(GLsizei, GLuint*) = nullptr;
		void (GL_APIENTRY* _glDeleteVertexArrays)(GLsizei, const GLuint*) = nullptr;
		void (GL_APIENTRY* _glBindVertexArray)(GLuint) = nullptr;

		void ResolveVertexArrayEntryPoints()
		{
			if DEATH_LIKELY(_glGenVertexArrays != nullptr) {
				return;
			}

			_glGenVertexArrays = reinterpret_cast<void (GL_APIENTRY*)(GLsizei, GLuint*)>(eglGetProcAddress("glGenVertexArraysOES"));
			_glDeleteVertexArrays = reinterpret_cast<void (GL_APIENTRY*)(GLsizei, const GLuint*)>(eglGetProcAddress("glDeleteVertexArraysOES"));
			_glBindVertexArray = reinterpret_cast<void (GL_APIENTRY*)(GLuint)>(eglGetProcAddress("glBindVertexArrayOES"));

			FATAL_ASSERT_MSG(_glGenVertexArrays != nullptr && _glDeleteVertexArrays != nullptr && _glBindVertexArray != nullptr,
				"GL_OES_vertex_array_object is required by the OpenGL|ES 2.0 profile but not provided by this context");
		}
	}
}

#	define glGenVertexArrays _glGenVertexArrays
#	define glDeleteVertexArrays _glDeleteVertexArrays
#	define glBindVertexArray _glBindVertexArray
#endif

namespace nCine::RHI::GL
{
	unsigned int GLVertexArrayObject::_boundVAO = 0;

	GLVertexArrayObject::GLVertexArrayObject()
		: _glHandle(0)
	{
#if defined(RHI_GL_PROFILE_ES2) && !defined(DEATH_TARGET_VITA)
		ResolveVertexArrayEntryPoints();
#endif
		glGenVertexArrays(1, &_glHandle);
		GL_LOG_ERRORS();
	}

	GLVertexArrayObject::~GLVertexArrayObject()
	{
		if (_boundVAO == _glHandle) {
			Unbind();
		}

		glDeleteVertexArrays(1, &_glHandle);
		GL_LOG_ERRORS();
	}

	bool GLVertexArrayObject::Bind() const
	{
		if (_boundVAO != _glHandle) {
			glBindVertexArray(_glHandle);
			GL_LOG_ERRORS();
			_boundVAO = _glHandle;
			return true;
		}
		return false;
	}

	bool GLVertexArrayObject::Unbind()
	{
		if (_boundVAO != 0) {
			glBindVertexArray(0);
			GL_LOG_ERRORS();
			_boundVAO = 0;
			return true;
		}
		return false;
	}

	void GLVertexArrayObject::SetObjectLabel(StringView label)
	{
		GLDebug::SetObjectLabel(GLDebug::LabelTypes::VertexArray, _glHandle, label);
	}
}
