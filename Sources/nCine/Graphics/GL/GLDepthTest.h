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

		static bool IsEnabled() {
			return state_.enabled;
		}
		static void Enable();
		static void Disable();

		static bool IsDepthMaskEnabled() {
			return state_.depthMaskEnabled;
		}
		static void EnableDepthMask();
		static void DisableDepthMask();

		static State GetState() {
			return state_;
		}
		static void SetState(State newState);

	private:
		static State state_;
	};

}
