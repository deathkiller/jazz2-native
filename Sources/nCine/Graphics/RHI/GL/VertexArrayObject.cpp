#if defined(WITH_RHI_GL)
#include "VertexArrayObject.h"
#include "Debug.h"

namespace nCine::RHI
{
	unsigned int VertexArrayObject::boundVAO_ = 0;

	VertexArrayObject::VertexArrayObject()
		: glHandle_(0)
	{
		glGenVertexArrays(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	VertexArrayObject::~VertexArrayObject()
	{
		if (boundVAO_ == glHandle_) {
			Unbind();
		}

		glDeleteVertexArrays(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	bool VertexArrayObject::Bind() const
	{
		if (boundVAO_ != glHandle_) {
			glBindVertexArray(glHandle_);
			GL_LOG_ERRORS();
			boundVAO_ = glHandle_;
			return true;
		}
		return false;
	}

	bool VertexArrayObject::Unbind()
	{
		if (boundVAO_ != 0) {
			glBindVertexArray(0);
			GL_LOG_ERRORS();
			boundVAO_ = 0;
			return true;
		}
		return false;
	}

	void VertexArrayObject::SetObjectLabel(StringView label)
	{
		Debug::SetObjectLabel(Debug::LabelTypes::VertexArray, glHandle_, label);
	}
}

#endif // defined(WITH_RHI_GL)
