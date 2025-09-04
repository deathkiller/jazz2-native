#pragma once

#include "RenderCommand.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/// Sorts and issues the render commands collected by the scenegraph visit
	class RenderQueue
	{
	public:
		RenderQueue();

		/// Returns `true` if the queue does not contain any render commands
		bool IsEmpty() const;

		/// Adds a draw command to the queue
		void AddCommand(RenderCommand* command);

		/// Sorts the queues, create batches and commits commands
		void SortAndCommit();
		/// Issues every render command in order
		void Draw();

		/// Clears all the queues and resets the render batcher
		void Clear();

	private:
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
