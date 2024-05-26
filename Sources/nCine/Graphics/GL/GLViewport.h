#pragma once

#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"

#include "../../Primitives/Rect.h"

namespace nCine
{
	/// A class to handle OpenGL viewport
	class GLViewport
	{
		friend class IGfxDevice; // for `initRect()`

	public:
		GLViewport() = delete;
		~GLViewport() = delete;

		struct State
		{
			Recti rect = Recti(0, 0, 0, 0);
		};

		static Recti rect() {
			return state_.rect;
		}
		static void setRect(const Recti& rect);
		static void setRect(GLint x, GLint y, GLsizei width, GLsizei height);

		static State state() {
			return state_;
		}
		static void setState(State newState);

	private:
		static State state_;

		static void initRect(GLint x, GLint y, GLsizei width, GLsizei height);
	};

}
