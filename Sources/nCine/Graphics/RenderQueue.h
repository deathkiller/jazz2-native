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
		/** @brief Layer past the highest one a command can have, so a split at it splits nothing */
		static constexpr std::uint32_t NoSplitLayer = 0x10000;

		RenderQueue();

		/** @brief Returns `true` if the queue does not contain any render commands */
		bool IsEmpty() const;

		/** @brief Adds a draw command to the queue */
		void AddCommand(RenderCommand* command);

		/**
		 * @brief Sorts the queues, builds batches and commits the commands
		 *
		 * No batch spans @p splitLayer, so the queue can then be drawn in two parts, the commands below it and the
		 * rest (see @ref Draw()).
		 */
		void SortAndCommit(std::uint32_t splitLayer = NoSplitLayer);
		/**
		 * @brief Issues the render commands of the given layers in order
		 *
		 * Draws the commands from @p minLayer up to (but not including) @p endLayer, every command by default.
		 */
		void Draw(std::uint32_t minLayer = 0, std::uint32_t endLayer = NoSplitLayer);

		/** @brief Clears all the queues and resets the render batcher */
		void Clear();

	private:
		/** @brief Array of opaque render command pointers */
		SmallVector<RenderCommand*, 0> _opaqueQueue;
		/** @brief Array of opaque batched render command pointers */
		SmallVector<RenderCommand*, 0> _opaqueBatchedQueue;
		/** @brief Array of transparent render command pointers */
		SmallVector<RenderCommand*, 0> _transparentQueue;
		/** @brief Array of transparent batched render command pointers */
		SmallVector<RenderCommand*, 0> _transparentBatchedQueue;
	};

}
