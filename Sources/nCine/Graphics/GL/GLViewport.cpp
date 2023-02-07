#include "GLViewport.h"

namespace nCine
{
	GLViewport::State GLViewport::state_;

	void GLViewport::setRect(const Recti& rect)
	{
		if (rect.X != state_.rect.X || rect.Y != state_.rect.Y ||
			rect.W != state_.rect.W || rect.H != state_.rect.H) {
			FATAL_ASSERT(rect.W >= 0 && rect.H >= 0);
			glViewport(rect.X, rect.Y, rect.W, rect.H);
			state_.rect = rect;
		}
	}

	void GLViewport::setRect(GLint x, GLint y, GLsizei width, GLsizei height)
	{
		setRect(Recti(x, y, width, height));
	}

	void GLViewport::setState(State newState)
	{
		setRect(newState.rect);
		state_ = newState;
	}

	void GLViewport::initRect(GLint x, GLint y, GLsizei width, GLsizei height)
	{
		state_.rect.X = x;
		state_.rect.Y = y;
		state_.rect.W = width;
		state_.rect.H = height;
	}
}
