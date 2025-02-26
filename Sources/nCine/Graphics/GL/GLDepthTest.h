#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

namespace nCine
{
	/// Handles OpenGL depth test
	class GLDepthTest
	{
	public:
		GLDepthTest() = delete;
		~GLDepthTest() = delete;

		/// Holds the depth test state
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
