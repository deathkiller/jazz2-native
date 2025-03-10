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

		static Colorf color() {
			return state_.color;
		}
		static void setColor(const Colorf& color);
		static void setColor(float red, float green, float blue, float alpha);

		static State state() {
			return state_;
		}
		static void setState(State newState);

	private:
		static State state_;
	};

}
