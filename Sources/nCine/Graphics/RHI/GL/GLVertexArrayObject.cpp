#include "GLVertexArrayObject.h"
#include "GLDebug.h"

#if defined(RHI_GL_PROFILE_ES2) && !defined(DEATH_TARGET_VITA)
// Vertex array objects are not ES 2.0 core - this profile relies on GL_OES_vertex_array_object, whose
// entry points carry the OES suffix on a true ES2 context (ANGLE exports them this way, the extension is
// required by this engine's render loop, which binds all vertex state through VAOs). PS Vita's vitaGL is
// the exception, it exports the unsuffixed glGenVertexArrays()/glDeleteVertexArrays()/glBindVertexArray()
// directly, so no remap is applied there.
#	define glGenVertexArrays glGenVertexArraysOES
#	define glDeleteVertexArrays glDeleteVertexArraysOES
#	define glBindVertexArray glBindVertexArrayOES
#endif

namespace nCine::RHI::GL
{
	unsigned int GLVertexArrayObject::boundVAO_ = 0;

	GLVertexArrayObject::GLVertexArrayObject()
		: glHandle_(0)
	{
		glGenVertexArrays(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	GLVertexArrayObject::~GLVertexArrayObject()
	{
		if (boundVAO_ == glHandle_) {
			Unbind();
		}

		glDeleteVertexArrays(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	bool GLVertexArrayObject::Bind() const
	{
		if (boundVAO_ != glHandle_) {
			glBindVertexArray(glHandle_);
			GL_LOG_ERRORS();
			boundVAO_ = glHandle_;
			return true;
		}
		return false;
	}

	bool GLVertexArrayObject::Unbind()
	{
		if (boundVAO_ != 0) {
			glBindVertexArray(0);
			GL_LOG_ERRORS();
			boundVAO_ = 0;
			return true;
		}
		return false;
	}

	void GLVertexArrayObject::SetObjectLabel(StringView label)
	{
		GLDebug::SetObjectLabel(GLDebug::LabelTypes::VertexArray, glHandle_, label);
	}
}
