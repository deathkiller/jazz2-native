#pragma once

#include "RHI/RhiFwd.h"

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
		 * The two bounds come from different places and mean different things. The **maximum** is what the
		 * batched shader was compiled for --- a batch may not index past its instance array --- so a backend
		 * that publishes @relativeref{RHI::IRhiCapabilities,IntValues::MaxBatchSize} (or a build that sets
		 * @relativeref{AppConfiguration,fixedBatchSize}) supplies it, and the run loop clamps to the shader's
		 * own @cpp GetBatchSize() @ce as well. The **minimum** is only a worthwhile-ness threshold: a batch
		 * submits @cpp 6 * @ce its own size in vertices and its instance block is allocated from the sizes
		 * actually accumulated, so a batch below the maximum draws and costs only what it holds. Tying the
		 * two together would leave every run shorter than the maximum, and every remainder past a multiple
		 * of it, drawn one command at a time.
		 *
		 * @param srcQueue   Sorted source command queue
		 * @param destQueue  Destination queue that receives batched and pass-through commands
		 */
		void CreateBatches(const SmallVectorImpl<RenderCommand*>& srcQueue, SmallVectorImpl<RenderCommand*>& destQueue);
		/** @brief Marks all managed buffers as free for reuse in the next frame */
		void Reset();

		/** @brief Batched command filled with instances directly rather than collected from commands, see @ref BeginDirectBatch() */
		struct DirectBatch
		{
			/** @brief The batched command, or `nullptr` if none is set up */
			RenderCommand* Command = nullptr;
			/** @brief Instances block of @ref Command */
			RHI::UniformBlockCache* InstancesBlock = nullptr;
			/** @brief Where the instances go, instance `i` at @cpp Instances + i * Stride @ce */
			std::uint8_t* Instances = nullptr;
			/** @brief Size of one instance in bytes */
			std::uint32_t Stride = 0;
			/** @brief Number of instances there is room for */
			std::uint32_t Capacity = 0;
			/** @brief Number of instances written so far */
			std::uint32_t Count = 0;
		};

		/**
		 * @brief Sets up a batched command that the caller fills with instances directly
		 *
		 * For a producer that emits many instances of one material in a row --- the glyphs of a string above all ---
		 * and would otherwise build a render command for each of them, only for @ref CreateBatches() to copy all of
		 * them into a batch again. Each of those commands is cold by the time it is sorted and collected, and on a
		 * console with a few kilobytes of data cache that is most of what a glyph costs.
		 *
		 * The command draws the batched variant of the shader of @p refCommand with its material state and layer,
		 * exactly like a batch that @ref CreateBatches() collected from commands like @p refCommand. An instance is
		 * laid out like the instance block of @p refCommand, and it holds what that block would hold for a command of
		 * its own once committed --- the model matrix with the depth of the layer included. The caller writes
		 * instance `i` at @cpp Instances + i * Stride @ce, for as many as @ref DirectBatch::Capacity allows: up to
		 * @p maxInstances, fewer if the batched shader or the uniform buffer holds fewer. It can add the command to
		 * its render queue right away, @ref EndDirectBatch() then finishes it for the instances written so far and
		 * can be called again after more were added.
		 *
		 * The memory comes from the same per-frame buffers as the batches of @ref CreateBatches(), so the batch is
		 * valid until the end of the frame. The command comes from the same pool as theirs, unless the caller passes
		 * @p batchCommand, a command of its own that it keeps from frame to frame - which saves looking one up in the
		 * pool and setting up its material from scratch every time.
		 *
		 * @return `false` with @p batch left empty if batching is disabled or the shader has no batched variant that
		 *         draws without vertex data --- the caller then submits a command per instance as before
		 */
		bool BeginDirectBatch(RenderCommand& refCommand, std::uint32_t maxInstances, DirectBatch& batch, RenderCommand* batchCommand = nullptr);
		/** @brief Finishes a batch started by @ref BeginDirectBatch() for the instances written into it */
		void EndDirectBatch(DirectBatch& batch);

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
		SmallVector<ManagedBuffer, 0> _buffers;

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

		/** @brief Returns the smallest and the largest number of commands a batch may collect */
		static void GetBatchSizeLimits(std::uint32_t& minBatchSize, std::uint32_t& maxBatchSize);
		/** @brief Returns the size of one instance in a batch, the instance block of the unbatched shader in the std140 array layout */
		static std::uint32_t GetInstanceStride(const RHI::UniformBlockCache* singleInstanceBlock);
		/** @brief Returns the bytes a batch needs in front of its instances, for the loose uniforms and the other uniform blocks */
		static std::uint32_t GetNonInstanceUniformsSize(RenderCommand* batchCommand, const RenderCommand* refCommand, const RHI::UniformBlockCache* singleInstanceBlock);
		/** @brief Copies the uniforms of @p refCommand that are not per instance into @p batchCommand, which already has its uniform memory */
		static void CopyNonInstanceUniforms(RenderCommand* batchCommand, const RenderCommand* refCommand, const RHI::UniformBlockCache* singleInstanceBlock, bool commandAdded);
		/** @brief Gives @p batchCommand the textures, blending, layer and visit order of @p refCommand */
		static void CopyMaterialState(RenderCommand* batchCommand, const RenderCommand* refCommand);

		/** @brief Reserves a contiguous region from a managed buffer, creating a new one if needed */
		std::uint8_t* AcquireMemory(std::uint32_t bytes);
		/** @brief Appends a new managed RAM buffer of the specified size */
		void CreateBuffer(std::uint32_t size);
	};

}
