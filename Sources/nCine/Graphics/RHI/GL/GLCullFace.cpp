#include "GLCullFace.h"

namespace nCine::RHI::GL
{
	GLCullFace::State GLCullFace::_state;

	void GLCullFace::Enable()
	{
		if (_state.enabled == false) {
			glEnable(GL_CULL_FACE);
			_state.enabled = true;
		}
	}

	void GLCullFace::Disable()
	{
		if (_state.enabled == true) {
			glDisable(GL_CULL_FACE);
			_state.enabled = false;
		}
	}

	void GLCullFace::SetMode(GLenum mode)
	{
		if (mode != _state.mode) {
			glCullFace(mode);
			_state.mode = mode;
		}
	}

	void GLCullFace::Reapply()
	{
		if (_state.enabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
		glCullFace(_state.mode);
	}

	void GLCullFace::SetState(State newState)
	{
		if (newState.enabled) {
			Enable();
		} else {
			Disable();
		}
		SetMode(newState.mode);

		_state = newState;
	}
}
