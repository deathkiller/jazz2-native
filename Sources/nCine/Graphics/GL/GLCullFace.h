#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

namespace nCine
{
	/// Handles OpenGL face culling
	class GLCullFace
	{
	public:
		GLCullFace() = delete;
		~GLCullFace() = delete;

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
