#include "GLScissorTest.h"
#include "../../../../Main.h"

namespace nCine::RHI::GL
{
	GLScissorTest::State GLScissorTest::_state;

	void GLScissorTest::Enable(const Recti& rect)
	{
		if (!_state.enabled) {
			glEnable(GL_SCISSOR_TEST);
			_state.enabled = true;
		}

		if (rect.X != _state.rect.X || rect.Y != _state.rect.Y ||
			rect.W != _state.rect.W || rect.H != _state.rect.H) {
			FATAL_ASSERT(rect.W >= 0 && rect.H >= 0);
			glScissor(rect.X, rect.Y, rect.W, rect.H);
			_state.rect = rect;
		}
	}

	void GLScissorTest::Enable(GLint x, GLint y, GLsizei width, GLsizei height)
	{
		Enable(Recti(x, y, width, height));
	}

	void GLScissorTest::Enable()
	{
		if (!_state.enabled) {
			FATAL_ASSERT(_state.rect.W >= 0 && _state.rect.H >= 0);
			glEnable(GL_SCISSOR_TEST);
			_state.enabled = true;
		}
	}

	void GLScissorTest::Disable()
	{
		if (_state.enabled) {
			glDisable(GL_SCISSOR_TEST);
			_state.enabled = false;
		}
	}

	void GLScissorTest::Reapply()
	{
		if (_state.enabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
		glScissor(_state.rect.X, _state.rect.Y, _state.rect.W, _state.rect.H);
	}

	void GLScissorTest::SetState(State newState)
	{
		if (newState.enabled) {
			Enable(newState.rect);
		} else {
			Disable();
		}
		_state = newState;
	}
}
