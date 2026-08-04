#include "GLViewport.h"

namespace nCine::RHI::GL
{
	GLViewport::State GLViewport::_state;

	void GLViewport::SetRect(const Recti& rect)
	{
		if (rect.X != _state.rect.X || rect.Y != _state.rect.Y ||
			rect.W != _state.rect.W || rect.H != _state.rect.H) {
			FATAL_ASSERT(rect.W >= 0 && rect.H >= 0);
			glViewport(rect.X, rect.Y, rect.W, rect.H);
			_state.rect = rect;
		}
	}

	void GLViewport::SetRect(GLint x, GLint y, GLsizei width, GLsizei height)
	{
		SetRect(Recti(x, y, width, height));
	}

	void GLViewport::Reapply()
	{
		glViewport(_state.rect.X, _state.rect.Y, _state.rect.W, _state.rect.H);
	}

	void GLViewport::SetState(State newState)
	{
		SetRect(newState.rect);
		_state = newState;
	}

	void GLViewport::InitRect(GLint x, GLint y, GLsizei width, GLsizei height)
	{
		_state.rect.X = x;
		_state.rect.Y = y;
		_state.rect.W = width;
		_state.rect.H = height;
	}
}
