#include "GLVertexArrayObject.h"
#include "GLDebug.h"

namespace nCine
{
	unsigned int GLVertexArrayObject::boundVAO_ = 0;

	GLVertexArrayObject::GLVertexArrayObject()
		: glHandle_(0)
	{
#if !defined(WITH_OPENGL2)
		glGenVertexArrays(1, &glHandle_);
		GL_LOG_ERRORS();
#endif
	}

	GLVertexArrayObject::~GLVertexArrayObject()
	{
#if !defined(WITH_OPENGL2)
		if (boundVAO_ == glHandle_) {
			Unbind();
		}

		glDeleteVertexArrays(1, &glHandle_);
		GL_LOG_ERRORS();
#endif
	}

	bool GLVertexArrayObject::Bind() const
	{
#if !defined(WITH_OPENGL2)
		if (boundVAO_ != glHandle_) {
			glBindVertexArray(glHandle_);
			GL_LOG_ERRORS();
			boundVAO_ = glHandle_;
			return true;
		}
#endif
		return false;
	}

	bool GLVertexArrayObject::Unbind()
	{
#if !defined(WITH_OPENGL2)
		if (boundVAO_ != 0) {
			glBindVertexArray(0);
			GL_LOG_ERRORS();
			boundVAO_ = 0;
			return true;
		}
#endif
		return false;
	}

	void GLVertexArrayObject::SetObjectLabel(StringView label)
	{
		GLDebug::SetObjectLabel(GLDebug::LabelTypes::VertexArray, glHandle_, label);
	}
}
