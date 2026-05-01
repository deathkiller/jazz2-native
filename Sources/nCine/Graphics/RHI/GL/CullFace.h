#pragma once

#if defined(WITH_RHI_GL)

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

namespace nCine::RHI
{
	/// Handles OpenGL face culling
	class CullFace
	{
	public:
		CullFace() = delete;
		~CullFace() = delete;

		/// Holds the face culling state
		struct State
		{
			bool enabled = false;
			GLenum mode = GL_BACK;
		};

		static bool IsEnabled() {
			return state_.enabled;
		}
		static void Enable();
		static void Disable();
		static void SetMode(GLenum mode);

		static State GetState() {
			return state_;
		}
		static void SetState(State newState);

	private:
		static State state_;
	};

}

#endif // defined(WITH_RHI_GL)
