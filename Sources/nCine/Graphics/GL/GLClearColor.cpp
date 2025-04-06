#include "GLClearColor.h"

namespace nCine
{
	GLClearColor::State GLClearColor::state_;

	void GLClearColor::SetColor(const Colorf& color)
	{
		if (color.R != state_.color.R || color.G != state_.color.G || color.B != state_.color.B || color.A != state_.color.A) {
			glClearColor(color.R, color.G, color.B, color.A);
			state_.color = color;
		}
	}

	void GLClearColor::SetColor(float red, float green, float blue, float alpha)
	{
		SetColor(Colorf(red, green, blue, alpha));
	}

	void GLClearColor::SetState(State newState)
	{
		SetColor(newState.color);
		state_ = newState;
	}
}
