#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

namespace nCine::RHI::GL
{
	/**
		@brief Manages the OpenGL depth test state
		
		Static wrapper that caches the current depth test state (`GL_DEPTH_TEST`
		enable flag and the depth buffer write mask set with `glDepthMask`) so that
		redundant OpenGL calls are skipped when the requested state matches the
		cached one.
	*/
	class GLDepthTest
	{
	public:
		GLDepthTest() = delete;
		~GLDepthTest() = delete;

		/**
		 * @brief Holds the cached depth test state
		 */
		struct State
		{
			/** @brief Whether the depth test is enabled */
			bool enabled = false;
			/** @brief Whether writing to the depth buffer is enabled */
			bool depthMaskEnabled = true;
		};

		/** @brief Returns whether the depth test is enabled */
		static bool IsEnabled() {
			return state_.enabled;
		}
		/** @brief Enables the depth test */
		static void Enable();
		/** @brief Disables the depth test */
		static void Disable();

		/** @brief Returns whether writing to the depth buffer is enabled */
		static bool IsDepthMaskEnabled() {
			return state_.depthMaskEnabled;
		}
		/** @brief Enables writing to the depth buffer */
		static void EnableDepthMask();
		/** @brief Disables writing to the depth buffer */
		static void DisableDepthMask();

		/** @brief Returns the whole cached depth test state */
		static State GetState() {
			return state_;
		}
		/** @brief Restores the whole depth test state */
		static void SetState(State newState);

	private:
		static State state_;
	};

}
