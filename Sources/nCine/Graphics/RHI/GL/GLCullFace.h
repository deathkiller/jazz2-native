#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

namespace nCine::RhiGL
{
	/**
		@brief Manages the OpenGL face culling state
		
		Static wrapper that caches the current face culling state (`GL_CULL_FACE`
		enable flag and the `glCullFace` mode) so that redundant OpenGL calls are
		skipped when the requested state matches the cached one.
	*/
	class GLCullFace
	{
	public:
		GLCullFace() = delete;
		~GLCullFace() = delete;

		/**
		 * @brief Holds the cached face culling state
		 */
		struct State
		{
			/** @brief Whether face culling is enabled */
			bool enabled = false;
			/** @brief Culled face mode (`GL_FRONT`, `GL_BACK` or `GL_FRONT_AND_BACK`) */
			GLenum mode = GL_BACK;
		};

		/** @brief Returns whether face culling is enabled */
		static bool IsEnabled() {
			return state_.enabled;
		}
		/** @brief Enables face culling */
		static void Enable();
		/** @brief Disables face culling */
		static void Disable();
		/**
		 * @brief Sets the culled face mode
		 *
		 * @param mode	Culled face mode (`GL_FRONT`, `GL_BACK` or `GL_FRONT_AND_BACK`)
		 */
		static void SetMode(GLenum mode);

		/** @brief Returns the whole cached face culling state */
		static State GetState() {
			return state_;
		}
		/** @brief Restores the whole face culling state */
		static void SetState(State newState);

	private:
		static State state_;
	};

}
