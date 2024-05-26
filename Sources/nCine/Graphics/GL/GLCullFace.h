#pragma once

#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"

namespace nCine
{
	/// A class to handle OpenGL face culling
	class GLCullFace
	{
	public:
		GLCullFace() = delete;
		~GLCullFace() = delete;

		struct State
		{
			bool enabled = false;
			GLenum mode = GL_BACK;
		};

		static bool isEnabled() {
			return state_.enabled;
		}
		static void enable();
		static void disable();
		static void setMode(GLenum mode);

		static State state() {
			return state_;
		}
		static void setState(State newState);

	private:
		static State state_;
	};

}
