#if defined(WITH_RHI_GL)
#include "ScissorTest.h"
#include "../../../../Main.h"

namespace nCine::RHI
{
	ScissorTest::State ScissorTest::state_;

	void ScissorTest::Enable(const Recti& rect)
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

	void ScissorTest::Enable(GLint x, GLint y, GLsizei width, GLsizei height)
	{
		Enable(Recti(x, y, width, height));
	}

	void ScissorTest::Enable()
	{
		if (!state_.enabled) {
			FATAL_ASSERT(state_.rect.W >= 0 && state_.rect.H >= 0);
			glEnable(GL_SCISSOR_TEST);
			state_.enabled = true;
		}
	}

	void ScissorTest::Disable()
	{
		if (state_.enabled) {
			glDisable(GL_SCISSOR_TEST);
			state_.enabled = false;
		}
	}

	void ScissorTest::SetState(State newState)
	{
		if (newState.enabled) {
			Enable(newState.rect);
		} else {
			Disable();
		}
		state_ = newState;
	}
}

#endif // defined(WITH_RHI_GL)
