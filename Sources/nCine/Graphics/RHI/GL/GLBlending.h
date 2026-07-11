#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

namespace nCine::RhiGL
{
	/**
		@brief Manages the OpenGL blending state
		
		Static wrapper that caches the current blending state (`GL_BLEND` enable flag
		and the `glBlendFunc`/`glBlendFuncSeparate` factors) so that redundant OpenGL
		calls are skipped when the requested state matches the cached one.
	*/
	class GLBlending
	{
	public:
		GLBlending() = delete;
		~GLBlending() = delete;

		/**
		 * @brief Holds the cached blending state
		 */
		struct State
		{
			/** @brief Whether blending is enabled */
			bool enabled = false;
			/** @brief Source RGB blend factor */
			GLenum srcRgb = GL_ONE;
			/** @brief Destination RGB blend factor */
			GLenum dstRgb = GL_ZERO;
			/** @brief Source alpha blend factor */
			GLenum srcAlpha = GL_ONE;
			/** @brief Destination alpha blend factor */
			GLenum dstAlpha = GL_ZERO;
		};

		/** @brief Returns whether blending is enabled */
		static bool IsEnabled() {
			return state_.enabled;
		}
		/** @brief Enables blending */
		static void Enable();
		/** @brief Disables blending */
		static void Disable();
		/**
		 * @brief Sets the same source and destination blend factors for both RGB and alpha
		 *
		 * @param sfactor	Source blend factor for RGB and alpha
		 * @param dfactor	Destination blend factor for RGB and alpha
		 */
		static void SetBlendFunc(GLenum sfactor, GLenum dfactor);
		/**
		 * @brief Sets separate source and destination blend factors for RGB and alpha
		 *
		 * @param srcRgb	Source blend factor for RGB
		 * @param dstRgb	Destination blend factor for RGB
		 * @param srcAlpha	Source blend factor for alpha
		 * @param dstAlpha	Destination blend factor for alpha
		 */
		static void SetBlendFunc(GLenum srcRgb, GLenum dstRgb, GLenum srcAlpha, GLenum dstAlpha);

		/** @brief Returns the whole cached blending state */
		static State GetState() {
			return state_;
		}
		/** @brief Restores the whole blending state */
		static void SetState(State newState);

	private:
		static State state_;
	};

}
