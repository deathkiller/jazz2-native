#pragma once

#include "Viewport.h"

namespace nCine
{
	/**
		@brief Viewport that renders directly to the screen
		
		The final viewport in the chain, drawing to the default framebuffer instead of a texture.
		It is created and driven by @ref Application, which resizes it to follow the window.
	*/
	class ScreenViewport : public Viewport
	{
		friend class Application;

	public:
		/** @brief Creates the screen viewport */
		ScreenViewport();

		ScreenViewport(const ScreenViewport&) = delete;
		ScreenViewport& operator=(const ScreenViewport&) = delete;

		/** @brief Changes the size, viewport rectangle and projection matrix of the screen viewport */
		void Resize(std::int32_t width, std::int32_t height);

	private:
		void Update();
		void Visit();
		void SortAndCommitQueue();
		void Draw();
	};
}
