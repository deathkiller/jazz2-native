#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "../../../Primitives/Rect.h"

namespace nCine::RHI::GL
{
	/**
		@brief Manages the OpenGL viewport rectangle
		
		Static wrapper that caches the current viewport rectangle (set with
		`glViewport`) so that redundant OpenGL calls are skipped when the requested
		rectangle matches the cached one.
	*/
	class GLViewport
	{
		friend class GLDevice; // for `InitRect()`

	public:
		GLViewport() = delete;
		~GLViewport() = delete;

		/**
		 * @brief Holds the cached viewport state
		 */
		struct State
		{
			/** @brief Viewport rectangle */
			Recti rect = Recti(0, 0, 0, 0);
		};

		/** @brief Returns the current viewport rectangle */
		static Recti GetRect() {
			return state_.rect;
		}
		/**
		 * @brief Sets the viewport rectangle
		 *
		 * @param rect	Viewport rectangle
		 */
		static void SetRect(const Recti& rect);
		/**
		 * @brief Sets the viewport rectangle
		 *
		 * @param x			Left coordinate of the viewport rectangle
		 * @param y			Bottom coordinate of the viewport rectangle
		 * @param width		Width of the viewport rectangle
		 * @param height	Height of the viewport rectangle
		 */
		static void SetRect(GLint x, GLint y, GLsizei width, GLsizei height);

		/** @brief Returns the whole cached viewport state */
		static State GetState() {
			return state_;
		}
		/** @brief Restores the whole viewport state */
		static void SetState(State newState);

	private:
		static State state_;

		static void InitRect(GLint x, GLint y, GLsizei width, GLsizei height);
	};
}
