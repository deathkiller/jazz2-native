#include "GLScissorTest.h"
#include "../../../Common.h"

namespace nCine
{
	GLScissorTest::State GLScissorTest::state_;

	void GLScissorTest::enable(const Recti& rect)
	{
		if (!state_.enabled) {
			glEnable(GL_SCISSOR_TEST);
			state_.enabled = true;
		}

		if (rect.X != state_.rect.X || rect.Y != state_.rect.Y ||
			rect.W != state_.rect.W || rect.H != state_.rect.H) {
			FATAL_ASSERT(rect.W >= 0 && rect.H >= 0);
			glScissor(rect.X, rect.Y, rect.W, rect.H);
			state_.rect = rect;
		}
	}

	void GLScissorTest::enable(GLint x, GLint y, GLsizei width, GLsizei height)
	{
		enable(Recti(x, y, width, height));
	}

	void GLScissorTest::enable()
	{
		if (!state_.enabled) {
			FATAL_ASSERT(state_.rect.W >= 0 && state_.rect.H >= 0);
			glEnable(GL_SCISSOR_TEST);
			state_.enabled = true;
		}
	}

	void GLScissorTest::disable()
	{
		if (state_.enabled) {
			glDisable(GL_SCISSOR_TEST);
			state_.enabled = false;
		}
	}

	void GLScissorTest::setState(State newState)
	{
		if (newState.enabled) {
			enable(newState.rect);
		} else {
			disable();
		}
		state_ = newState;
	}
}
