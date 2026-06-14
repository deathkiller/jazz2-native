#pragma once

#include <memory>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class RenderCommand;

	/**
		@brief Merges compatible render commands into fewer draw calls
		
		Scans a sorted queue of @ref RenderCommand objects and groups runs that share the same material sort
		key and primitive type into single batched commands, copying their per-instance uniform blocks,
		vertices and indices into shared memory. Reduces the number of draw calls issued each frame.
	*/
	class RenderBatcher
	{
	public:
		RenderBatcher();

		/**
		 * @brief Collects consecutive compatible commands into batched commands
		 *
		 * Commands that cannot be batched, or runs shorter than the minimum batch size, are passed through to
		 * the destination queue unchanged.
		 *
		 * @param srcQueue   Sorted source command queue
		 * @param destQueue  Destination queue that receives batched and pass-through commands
		 */
		void CreateBatches(const SmallVectorImpl<RenderCommand*>& srcQueue, SmallVectorImpl<RenderCommand*>& destQueue);
		/** @brief Marks all managed buffers as free for reuse in the next frame */
		void Reset();

	private:
		/** @brief Maximum uniform block size supported by the driver, used as the per-buffer capacity */
		static std::uint32_t UboMaxSize;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ManagedBuffer
		{
			ManagedBuffer()
				: size(0), freeSpace(0) {}

			std::uint32_t size;
			std::uint32_t freeSpace;
			std::unique_ptr<std::uint8_t[]> buffer;
		};
#endif

		/**
		 * @brief Memory buffers used to collect UBO data before committing it
		 *
		 * @note It is a RAM buffer and cannot be handled by the `RenderBuffersManager`.
		 */
		SmallVector<ManagedBuffer, 0> buffers_;

		/**
		 * @brief Builds a single batched command from a run of source commands
		 *
		 * Consumes as many commands from the `[start, end)` range as fit within the UBO, VBO and IBO size
		 * limits and reports the first uncollected command through `nextStart`.
		 *
		 * @param start      Iterator to the first command to collect
		 * @param end        Iterator past the last candidate command
		 * @param nextStart  Receives the iterator to the first command that was not collected
		 * @return Batched render command
		 */
		RenderCommand* CollectCommands(SmallVectorImpl<RenderCommand*>::const_iterator start, SmallVectorImpl<RenderCommand*>::const_iterator end, SmallVectorImpl<RenderCommand*>::const_iterator& nextStart);

		/** @brief Reserves a contiguous region from a managed buffer, creating a new one if needed */
		std::uint8_t* AcquireMemory(std::uint32_t bytes);
		/** @brief Appends a new managed RAM buffer of the specified size */
		void CreateBuffer(std::uint32_t size);
	};

}
