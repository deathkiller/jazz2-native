#include "GLCullFace.h"

namespace nCine
{
	GLCullFace::State GLCullFace::state_;

	void GLCullFace::Enable()
	{
		if (state_.enabled == false) {
			glEnable(GL_CULL_FACE);
			state_.enabled = true;
		}
	}

	void GLCullFace::Disable()
	{
		if (state_.enabled == true) {
			glDisable(GL_CULL_FACE);
			state_.enabled = false;
		}
	}

	void GLCullFace::SetMode(GLenum mode)
	{
		if (mode != state_.mode) {
			glCullFace(mode);
			state_.mode = mode;
		}
	}

	void GLCullFace::SetState(State newState)
	{
		if (newState.enabled) {
			Enable();
		} else {
			Disable();
		}
		SetMode(newState.mode);

		state_ = newState;
	}
}
