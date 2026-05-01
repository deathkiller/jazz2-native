#if defined(WITH_RHI_GL)
#include "Viewport.h"

namespace nCine::RHI
{
	Viewport::State Viewport::state_;

	void Viewport::SetRect(const Recti& rect)
	{
		if (rect.X != state_.rect.X || rect.Y != state_.rect.Y ||
			rect.W != state_.rect.W || rect.H != state_.rect.H) {
			FATAL_ASSERT(rect.W >= 0 && rect.H >= 0);
			glViewport(rect.X, rect.Y, rect.W, rect.H);
			state_.rect = rect;
		}
	}

	void Viewport::SetRect(GLint x, GLint y, GLsizei width, GLsizei height)
	{
		SetRect(Recti(x, y, width, height));
	}

	void Viewport::SetState(State newState)
	{
		SetRect(newState.rect);
		state_ = newState;
	}

	void Viewport::InitRect(GLint x, GLint y, GLsizei width, GLsizei height)
	{
		state_.rect.X = x;
		state_.rect.Y = y;
		state_.rect.W = width;
		state_.rect.H = height;
	}
}

#endif // defined(WITH_RHI_GL)
