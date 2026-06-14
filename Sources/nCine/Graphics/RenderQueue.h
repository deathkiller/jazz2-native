#pragma once

#include "RenderCommand.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Sorts and issues the render commands collected during a scene graph visit
		
		Drawable nodes append their commands during the visit phase. @ref SortAndCommit() then
		separates opaque from transparent commands, sorts each group, builds batches and uploads
		their data, and @ref Draw() finally issues them to OpenGL in the proper order.
	*/
	class RenderQueue
	{
	public:
		RenderQueue();

		/** @brief Returns `true` if the queue does not contain any render commands */
		bool IsEmpty() const;

		/** @brief Adds a draw command to the queue */
		void AddCommand(RenderCommand* command);

		/** @brief Sorts the queues, builds batches and commits the commands */
		void SortAndCommit();
		/** @brief Issues every render command in order */
		void Draw();

		/** @brief Clears all the queues and resets the render batcher */
		void Clear();

	private:
		/** @brief Array of opaque render command pointers */
		SmallVector<RenderCommand*, 0> opaqueQueue_;
		/** @brief Array of opaque batched render command pointers */
		SmallVector<RenderCommand*, 0> opaqueBatchedQueue_;
		/** @brief Array of transparent render command pointers */
		SmallVector<RenderCommand*, 0> transparentQueue_;
		/** @brief Array of transparent batched render command pointers */
		SmallVector<RenderCommand*, 0> transparentBatchedQueue_;
	};

}
