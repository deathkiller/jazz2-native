#include "GLVertexArrayObject.h"
#include "GLDebug.h"

namespace nCine
{
	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////

	unsigned int GLVertexArrayObject::boundVAO_ = 0;

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS AND DESTRUCTOR
	///////////////////////////////////////////////////////////

	GLVertexArrayObject::GLVertexArrayObject()
		: glHandle_(0)
	{
		glGenVertexArrays(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	GLVertexArrayObject::~GLVertexArrayObject()
	{
		if (boundVAO_ == glHandle_)
			unbind();

		glDeleteVertexArrays(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	bool GLVertexArrayObject::bind() const
	{
		if (boundVAO_ != glHandle_) {
			glBindVertexArray(glHandle_);
			GL_LOG_ERRORS();
			boundVAO_ = glHandle_;
			return true;
		}
		return false;
	}

	bool GLVertexArrayObject::unbind()
	{
		if (boundVAO_ != 0) {
			glBindVertexArray(0);
			GL_LOG_ERRORS();
			boundVAO_ = 0;
			return true;
		}
		return false;
	}

	void GLVertexArrayObject::setObjectLabel(const char* label)
	{
		GLDebug::objectLabel(GLDebug::LabelTypes::VertexArray, glHandle_, label);
	}

}
