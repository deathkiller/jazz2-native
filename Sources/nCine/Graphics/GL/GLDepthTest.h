#pragma once

#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"

namespace nCine
{
	/// A class to handle OpenGL depth test
	class GLDepthTest
	{
	public:
		GLDepthTest() = delete;
		~GLDepthTest() = delete;

		struct State
		{
			bool enabled = false;
			bool depthMaskEnabled = true;
		};

		static bool isEnabled() {
			return state_.enabled;
		}
		static void enable();
		static void disable();

		static bool isDepthMaskEnabled() {
			return state_.depthMaskEnabled;
		}
		static void enableDepthMask();
		static void disableDepthMask();

		static State state() {
			return state_;
		}
		static void setState(State newState);

	private:
		static State state_;
	};

}
