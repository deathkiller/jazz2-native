#include "GLDepthTest.h"

namespace nCine::RHI::GL
{
	GLDepthTest::State GLDepthTest::_state;

	void GLDepthTest::Enable()
	{
		if (_state.enabled == false) {
			glEnable(GL_DEPTH_TEST);
			_state.enabled = true;
		}
	}

	void GLDepthTest::Disable()
	{
		if (_state.enabled == true) {
			glDisable(GL_DEPTH_TEST);
			_state.enabled = false;
		}
	}

	void GLDepthTest::EnableDepthMask()
	{
		if (_state.depthMaskEnabled == false) {
			glDepthMask(GL_TRUE);
			_state.depthMaskEnabled = true;
		}
	}

	void GLDepthTest::DisableDepthMask()
	{
		if (_state.depthMaskEnabled == true) {
			glDepthMask(GL_FALSE);
			_state.depthMaskEnabled = false;
		}
	}

	void GLDepthTest::Reapply()
	{
		if (_state.enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
		glDepthMask(_state.depthMaskEnabled ? GL_TRUE : GL_FALSE);
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
		_state = newState;
	}
}
