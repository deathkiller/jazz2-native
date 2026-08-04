#include "GLClearColor.h"

namespace nCine::RHI::GL
{
	GLClearColor::State GLClearColor::_state;

	void GLClearColor::SetColor(const Colorf& color)
	{
		if (color.R != _state.color.R || color.G != _state.color.G || color.B != _state.color.B || color.A != _state.color.A) {
			glClearColor(color.R, color.G, color.B, color.A);
			_state.color = color;
		}
	}

	void GLClearColor::SetColor(float red, float green, float blue, float alpha)
	{
		SetColor(Colorf(red, green, blue, alpha));
	}

	void GLClearColor::Reapply()
	{
		glClearColor(_state.color.R, _state.color.G, _state.color.B, _state.color.A);
	}

	void GLClearColor::SetState(State newState)
	{
		SetColor(newState.color);
		_state = newState;
	}
}
