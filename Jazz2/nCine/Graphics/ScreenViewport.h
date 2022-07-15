#pragma once

#include "Viewport.h"

namespace nCine
{
	/// The class handling the screen viewport
	class ScreenViewport : public Viewport
	{
	public:
		/// Creates the screen viewport
		ScreenViewport();

	private:
		using Viewport::update;
		using Viewport::visit;
		void sortAndCommitQueue();
		void draw();

		/// Deleted copy constructor
		ScreenViewport(const ScreenViewport&) = delete;
		/// Deleted assignment operator
		ScreenViewport& operator=(const ScreenViewport&) = delete;

		friend class Application;
	};
}
