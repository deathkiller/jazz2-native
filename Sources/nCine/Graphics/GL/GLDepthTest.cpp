#include "GLDepthTest.h"

namespace nCine
{
	GLDepthTest::State GLDepthTest::state_;

	void GLDepthTest::Enable()
	{
		if (state_.enabled == false) {
			glEnable(GL_DEPTH_TEST);
			state_.enabled = true;
		}
	}

	void GLDepthTest::Disable()
	{
		if (state_.enabled == true) {
			glDisable(GL_DEPTH_TEST);
			state_.enabled = false;
		}
	}

	void GLDepthTest::EnableDepthMask()
	{
		if (state_.depthMaskEnabled == false) {
			glDepthMask(GL_TRUE);
			state_.depthMaskEnabled = true;
		}
	}

	void GLDepthTest::DisableDepthMask()
	{
		if (state_.depthMaskEnabled == true) {
			glDepthMask(GL_FALSE);
			state_.depthMaskEnabled = false;
		}
	}

	void GLDepthTest::SetState(State newState)
	{
		if (newState.enabled) {
			Enable();
		} else {
			Disable();
		}
		if (newState.depthMaskEnabled) {
			EnableDepthMask();
		} else {
			DisableDepthMask();
		}
		state_ = newState;
	}
}
