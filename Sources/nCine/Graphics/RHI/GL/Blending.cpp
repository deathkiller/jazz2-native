#if defined(WITH_RHI_GL)
#include "Blending.h"

namespace nCine::RHI
{
	Blending::State Blending::state_;

	void Blending::Enable()
	{
		if (state_.enabled == false) {
			glEnable(GL_BLEND);
			state_.enabled = true;
		}
	}

	void Blending::Disable()
	{
		if (state_.enabled == true) {
			glDisable(GL_BLEND);
			state_.enabled = false;
		}
	}

	void Blending::SetBlendFunc(GLenum sfactor, GLenum dfactor)
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

	void Blending::SetBlendFunc(GLenum srcRgb, GLenum dstRgb, GLenum srcAlpha, GLenum dstAlpha)
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

	void Blending::SetState(State newState)
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

#endif // defined(WITH_RHI_GL)
