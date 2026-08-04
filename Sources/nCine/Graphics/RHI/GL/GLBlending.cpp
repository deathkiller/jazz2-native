#include "GLBlending.h"

namespace nCine::RHI::GL
{
	GLBlending::State GLBlending::_state;

	void GLBlending::Enable()
	{
		if (_state.enabled == false) {
			glEnable(GL_BLEND);
			_state.enabled = true;
		}
	}

	void GLBlending::Disable()
	{
		if (_state.enabled == true) {
			glDisable(GL_BLEND);
			_state.enabled = false;
		}
	}

	void GLBlending::SetBlendFunc(GLenum sfactor, GLenum dfactor)
	{
		if (sfactor != _state.srcRgb || dfactor != _state.dstRgb ||
			sfactor != _state.srcAlpha || dfactor != _state.dstAlpha) {
			glBlendFunc(sfactor, dfactor);
			_state.srcRgb = sfactor;
			_state.dstRgb = dfactor;
			_state.srcAlpha = sfactor;
			_state.dstAlpha = dfactor;
		}
	}

	void GLBlending::SetBlendFunc(GLenum srcRgb, GLenum dstRgb, GLenum srcAlpha, GLenum dstAlpha)
	{
		if (srcRgb != _state.srcRgb || dstRgb != _state.dstRgb ||
			srcAlpha != _state.srcAlpha || dstAlpha != _state.dstAlpha) {
			glBlendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
			_state.srcRgb = srcRgb;
			_state.dstRgb = dstRgb;
			_state.srcAlpha = srcAlpha;
			_state.dstAlpha = dstAlpha;
		}
	}

	void GLBlending::Reapply()
	{
		if (_state.enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
		glBlendFuncSeparate(_state.srcRgb, _state.dstRgb, _state.srcAlpha, _state.dstAlpha);
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

		_state = newState;
	}
}
