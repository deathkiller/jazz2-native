#pragma once

#if defined(WITH_RHI_GL)

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "../../../Primitives/Rect.h"

namespace nCine
{
	class IGfxDevice;
}

namespace nCine::RHI
{
	/// Handles OpenGL viewport
	class Viewport
	{
		friend class nCine::IGfxDevice; // for `initRect()`

	public:
		Viewport() = delete;
		~Viewport() = delete;

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

#endif // defined(WITH_RHI_GL)
