#pragma once

#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"

namespace nCine
{
	/// A class to handle OpenGL blending
	class GLBlending
	{
	public:
		GLBlending() = delete;
		~GLBlending() = delete;

		struct State
		{
			bool enabled = false;
			GLenum srcRgb = GL_ONE;
			GLenum dstRgb = GL_ZERO;
			GLenum srcAlpha = GL_ONE;
			GLenum dstAlpha = GL_ZERO;
		};

		static bool isEnabled() {
			return state_.enabled;
		}
		static void enable();
		static void disable();
		static void setBlendFunc(GLenum sfactor, GLenum dfactor);
		static void setBlendFunc(GLenum srcRgb, GLenum dstRgb, GLenum srcAlpha, GLenum dstAlpha);

		static State state() {
			return state_;
		}
		static void setState(State newState);

	private:
		static State state_;
	};

}
