#include "GLBlending.h"

namespace nCine
{
	GLBlending::State GLBlending::state_;

	void GLBlending::Enable()
	{
		if (state_.enabled == false) {
			glEnable(GL_BLEND);
			state_.enabled = true;
		}
	}

	void GLBlending::Disable()
	{
		if (state_.enabled == true) {
			glDisable(GL_BLEND);
			state_.enabled = false;
		}
	}

	void GLBlending::SetBlendFunc(GLenum sfactor, GLenum dfactor)
	{
		if (sfactor != state_.srcRgb || dfactor != state_.dstRgb ||
			sfactor != state_.srcAlpha || dfactor != state_.dstAlpha) {
			glBlendFunc(sfactor, dfactor);
			state_.srcRgb = sfactor;
			state_.dstRgb = dfactor;
			state_.srcAlpha = sfactor;
			state_.dstAlpha = dfactor;
		}
	}

	void GLBlending::SetBlendFunc(GLenum srcRgb, GLenum dstRgb, GLenum srcAlpha, GLenum dstAlpha)
	{
		if (srcRgb != state_.srcRgb || dstRgb != state_.dstRgb ||
			srcAlpha != state_.srcAlpha || dstAlpha != state_.dstAlpha) {
			glBlendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
			state_.srcRgb = srcRgb;
			state_.dstRgb = dstRgb;
			state_.srcAlpha = srcAlpha;
			state_.dstAlpha = dstAlpha;
		}
	}

	void GLBlending::SetState(State newState)
	{
		if (newState.enabled)
			Enable();
		else
			Disable();

		if (newState.srcRgb == newState.srcAlpha && newState.dstRgb == newState.dstAlpha)
			SetBlendFunc(newState.srcRgb, newState.dstRgb);
		else
			SetBlendFunc(newState.srcRgb, newState.dstRgb, newState.srcAlpha, newState.dstAlpha);

		state_ = newState;
	}
}
