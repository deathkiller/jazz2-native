#if defined(WITH_RHI_GL)
#include "DepthTest.h"

namespace nCine::RHI
{
	DepthTest::State DepthTest::state_;

	void DepthTest::Enable()
	{
		if (state_.enabled == false) {
			glEnable(GL_DEPTH_TEST);
			state_.enabled = true;
		}
	}

	void DepthTest::Disable()
	{
		if (state_.enabled == true) {
			glDisable(GL_DEPTH_TEST);
			state_.enabled = false;
		}
	}

	void DepthTest::EnableDepthMask()
	{
		if (state_.depthMaskEnabled == false) {
			glDepthMask(GL_TRUE);
			state_.depthMaskEnabled = true;
		}
	}

	void DepthTest::DisableDepthMask()
	{
		if (state_.depthMaskEnabled == true) {
			glDepthMask(GL_FALSE);
			state_.depthMaskEnabled = false;
		}
	}

	void DepthTest::SetState(State newState)
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

#endif // defined(WITH_RHI_GL)
