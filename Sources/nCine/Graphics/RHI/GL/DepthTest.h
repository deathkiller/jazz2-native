#pragma once

#if defined(WITH_RHI_GL)

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

namespace nCine::RHI
{
	/// Handles OpenGL depth test
	class DepthTest
	{
	public:
		DepthTest() = delete;
		~DepthTest() = delete;

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

#endif // defined(WITH_RHI_GL)
