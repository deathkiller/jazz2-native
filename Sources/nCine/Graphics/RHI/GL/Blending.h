#pragma once

#if defined(WITH_RHI_GL)

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

namespace nCine::RHI
{
	/// Handles OpenGL blending
	class Blending
	{
	public:
		Blending() = delete;
		~Blending() = delete;

		/// Holds the blending state
		struct State
		{
			bool enabled = false;
			GLenum srcRgb = GL_ONE;
			GLenum dstRgb = GL_ZERO;
			GLenum srcAlpha = GL_ONE;
			GLenum dstAlpha = GL_ZERO;
		};

		static bool IsEnabled() {
			return state_.enabled;
		}
		static void Enable();
		static void Disable();
		static void SetBlendFunc(GLenum sfactor, GLenum dfactor);
		static void SetBlendFunc(GLenum srcRgb, GLenum dstRgb, GLenum srcAlpha, GLenum dstAlpha);

		static State GetState() {
			return state_;
		}
		static void SetState(State newState);

	private:
		static State state_;
	};

}

#endif // defined(WITH_RHI_GL)
