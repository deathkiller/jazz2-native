#pragma once

#include "Viewport.h"

namespace nCine
{
	/// Handles the screen viewport
	class ScreenViewport : public Viewport
	{
		friend class Application;

	public:
		/// Creates the screen viewport
		ScreenViewport();

		ScreenViewport(const ScreenViewport&) = delete;
		ScreenViewport& operator=(const ScreenViewport&) = delete;

		/// Changes the size, viewport rectangle and projection matrix of the screen viewport
		void Resize(std::int32_t width, std::int32_t height);

	private:
		void Update();
		void Visit();
		void SortAndCommitQueue();
		void Draw();
	};
}
