#if defined(WITH_RHI_GL)
#include "ClearColor.h"

namespace nCine::RHI
{
	ClearColor::State ClearColor::state_;

	void ClearColor::SetColor(const Colorf& color)
	{
		if (color.R != state_.color.R || color.G != state_.color.G || color.B != state_.color.B || color.A != state_.color.A) {
			glClearColor(color.R, color.G, color.B, color.A);
			state_.color = color;
		}
	}

	void ClearColor::SetColor(float red, float green, float blue, float alpha)
	{
		SetColor(Colorf(red, green, blue, alpha));
	}

	void ClearColor::SetState(State newState)
	{
		SetColor(newState.color);
		state_ = newState;
	}
}

#endif // defined(WITH_RHI_GL)
