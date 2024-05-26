#pragma once

#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"

#include "../../Primitives/Rect.h"

namespace nCine
{
	/// A class to handle OpenGL scissor test
	class GLScissorTest
	{
	public:
		GLScissorTest() = delete;
		~GLScissorTest() = delete;

		struct State
		{
			bool enabled = false;
			Recti rect = Recti(0, 0, 0, 0);
		};

		static bool isEnabled() {
			return state_.enabled;
		}
		static Recti rect() {
			return state_.rect;
		}
		static void enable(const Recti& rect);
		static void enable(GLint x, GLint y, GLsizei width, GLsizei height);
		static void enable();
		static void disable();

		static State state() {
			return state_;
		}
		static void setState(State newState);

	private:
		static State state_;
	};

}
