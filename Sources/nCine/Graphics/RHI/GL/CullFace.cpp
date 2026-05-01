#if defined(WITH_RHI_GL)
#include "CullFace.h"

namespace nCine::RHI
{
	CullFace::State CullFace::state_;

	void CullFace::Enable()
	{
		if (!state_.enabled) {
			glEnable(GL_CULL_FACE);
			state_.enabled = true;
		}
	}

	void CullFace::Disable()
	{
		if (state_.enabled) {
			glDisable(GL_CULL_FACE);
			state_.enabled = false;
		}
	}

	void CullFace::SetMode(GLenum mode)
	{
		if (mode != state_.mode) {
			glCullFace(mode);
			state_.mode = mode;
		}
	}

	void CullFace::SetState(State newState)
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

#endif // defined(WITH_RHI_GL)
