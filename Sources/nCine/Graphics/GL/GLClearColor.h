#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

#include "../../Primitives/Colorf.h"

namespace nCine
{
	/// Handles OpenGL clear color
	class GLClearColor
	{
	public:
		GLClearColor() = delete;
		~GLClearColor() = delete;

		/// Holds the clear color state
		struct State
		{
			Colorf color = Colorf(0.0f, 0.0f, 0.0f, 0.0f);
		};

		static Colorf GetColor() {
			return state_.color;
		}
		static void SetColor(const Colorf& color);
		static void SetColor(float red, float green, float blue, float alpha);

		static State GetState() {
			return state_;
		}
		static void SetState(State newState);

	private:
		static State state_;
	};

}
