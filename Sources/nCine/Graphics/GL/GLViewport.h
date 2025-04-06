#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

#include "../../Primitives/Rect.h"

namespace nCine
{
	/// Handles OpenGL viewport
	class GLViewport
	{
		friend class IGfxDevice; // for `initRect()`

	public:
		GLViewport() = delete;
		~GLViewport() = delete;

		/// Holds the viewport state
		struct State
		{
			Recti rect = Recti(0, 0, 0, 0);
		};

		static Recti GetRect() {
			return state_.rect;
		}
		static void SetRect(const Recti& rect);
		static void SetRect(GLint x, GLint y, GLsizei width, GLsizei height);

		static State GetState() {
			return state_;
		}
		static void SetState(State newState);

	private:
		static State state_;

		static void InitRect(GLint x, GLint y, GLsizei width, GLsizei height);
	};
}
