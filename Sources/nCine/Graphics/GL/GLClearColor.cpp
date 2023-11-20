#include "GLClearColor.h"

namespace nCine
{
	GLClearColor::State GLClearColor::state_;

	void GLClearColor::setColor(const Colorf& color)
	{
		if (color.R != state_.color.R || color.G != state_.color.G || color.B != state_.color.B || color.A != state_.color.A) {
			glClearColor(color.R, color.G, color.B, color.A);
			state_.color = color;
		}
	}

	void GLClearColor::setColor(float red, float green, float blue, float alpha)
	{
		setColor(Colorf(red, green, blue, alpha));
	}

	void GLClearColor::setState(State newState)
	{
		setColor(newState.color);
		state_ = newState;
	}
}
