#pragma once

#if defined(WITH_RHI_GL)

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "../../../Primitives/Rect.h"

namespace nCine::RHI
{
	/// Handles OpenGL scissor test
	class ScissorTest
	{
	public:
		ScissorTest() = delete;
		~ScissorTest() = delete;

		/// Holds the scissor test state
		struct State
		{
			bool enabled = false;
			Recti rect = Recti(0, 0, 0, 0);
		};

		static bool IsEnabled() {
			return state_.enabled;
		}
		static Recti GetRect() {
			return state_.rect;
		}
		static void Enable(const Recti& rect);
		static void Enable(GLint x, GLint y, GLsizei width, GLsizei height);
		static void Enable();
		static void Disable();

		static State GetState() {
			return state_;
		}
		static void SetState(State newState);

	private:
		static State state_;
	};

}

#endif // defined(WITH_RHI_GL)
