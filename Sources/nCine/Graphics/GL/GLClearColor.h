#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

#include "../../Primitives/Colorf.h"

namespace nCine
{
	/**
		@brief Manages the OpenGL clear color
		
		Static wrapper that caches the clear color used by `glClear` (set with
		`glClearColor`) so that redundant OpenGL calls are skipped when the requested
		color matches the cached one.
	*/
	class GLClearColor
	{
	public:
		GLClearColor() = delete;
		~GLClearColor() = delete;

		/**
		 * @brief Holds the cached clear color state
		 */
		struct State
		{
			/** @brief Clear color */
			Colorf color = Colorf(0.0f, 0.0f, 0.0f, 0.0f);
		};

		/** @brief Returns the current clear color */
		static Colorf GetColor() {
			return state_.color;
		}
		/** @brief Sets the clear color */
		static void SetColor(const Colorf& color);
		/**
		 * @brief Sets the clear color from individual components
		 *
		 * @param red	Red component
		 * @param green	Green component
		 * @param blue	Blue component
		 * @param alpha	Alpha component
		 */
		static void SetColor(float red, float green, float blue, float alpha);

		/** @brief Returns the whole cached clear color state */
		static State GetState() {
			return state_;
		}
		/** @brief Restores the whole clear color state */
		static void SetState(State newState);

	private:
		static State state_;
	};

}
