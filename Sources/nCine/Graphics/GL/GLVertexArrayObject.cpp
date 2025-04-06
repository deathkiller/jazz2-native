#include "GLVertexArrayObject.h"
#include "GLDebug.h"

namespace nCine
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

	void GLVertexArrayObject::SetObjectLabel(const char* label)
	{
		GLDebug::SetObjectLabel(GLDebug::LabelTypes::VertexArray, glHandle_, label);
	}
}
