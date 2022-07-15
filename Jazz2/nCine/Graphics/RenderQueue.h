#pragma once

#include "RenderCommand.h"

#include <SmallVector.h>

using namespace Death;

namespace nCine
{
	class Viewport;

	/// A class that sorts and issues the render commands collected by the scenegraph visit
	class RenderQueue
	{
	public:
		/// Constructor that sets the owning viewport
		explicit RenderQueue(Viewport& viewport);

		/// Returns true if the queue does not contain any render commands
		bool empty() const;

		/// Adds a draw command to the queue
		void addCommand(RenderCommand* command);

		/// Sorts the queues, create batches and commits commands
		void sortAndCommit();
		/// Issues every render command in order
		void draw();

		/// Returns the viewport that owns this render queue
		inline Viewport& viewport() const {
			return viewport_;
		}

	private:
		/// The viewport that owns this render queue
		Viewport& viewport_;

		/// Array of opaque render command pointers
		SmallVector<RenderCommand*, 0> opaqueQueue_;
		/// Array of opaque batched render command pointers
		SmallVector<RenderCommand*, 0> opaqueBatchedQueue_;
		/// Array of transparent render command pointers
		SmallVector<RenderCommand*, 0> transparentQueue_;
		/// Array of transparent batched render command pointers
		SmallVector<RenderCommand*, 0> transparentBatchedQueue_;
	};

}
