#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "../../../Primitives/Rect.h"

namespace nCine::RHI::GL
{
	/**
		@brief Manages the OpenGL scissor test state
		
		Static wrapper that caches the current scissor test state (`GL_SCISSOR_TEST`
		enable flag and the `glScissor` rectangle) so that redundant OpenGL calls are
		skipped when the requested state matches the cached one.
	*/
	class GLScissorTest
	{
	public:
		GLScissorTest() = delete;
		~GLScissorTest() = delete;

		/**
		 * @brief Holds the cached scissor test state
		 */
		struct State
		{
			/** @brief Whether the scissor test is enabled */
			bool enabled = false;
			/** @brief Scissor rectangle */
			Recti rect = Recti(0, 0, 0, 0);
		};

		/** @brief Returns whether the scissor test is enabled */
		static bool IsEnabled() {
			return state_.enabled;
		}
		/** @brief Returns the current scissor rectangle */
		static Recti GetRect() {
			return state_.rect;
		}
		/**
		 * @brief Enables the scissor test with the specified rectangle
		 *
		 * @param rect	Scissor rectangle
		 */
		static void Enable(const Recti& rect);
		/**
		 * @brief Enables the scissor test with the specified rectangle
		 *
		 * @param x			Left coordinate of the scissor rectangle
		 * @param y			Bottom coordinate of the scissor rectangle
		 * @param width		Width of the scissor rectangle
		 * @param height	Height of the scissor rectangle
		 */
		static void Enable(GLint x, GLint y, GLsizei width, GLsizei height);
		/** @brief Enables the scissor test reusing the cached rectangle */
		static void Enable();
		/** @brief Disables the scissor test */
		static void Disable();

		/** @brief Returns the whole cached scissor test state */
		static State GetState() {
			return state_;
		}
		/** @brief Restores the whole scissor test state */
		static void SetState(State newState);

	private:
		static State state_;
	};

}
