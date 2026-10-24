#pragma once

#include "RenderCommand.h"

#include <Containers/ArrayView.h>
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

#if defined(RHI_CAP_POSTPROCESSING) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Commands of a range of layers, a part of a queue drawn in parts (see @ref NextPart()) */
		struct Part
		{
			/** @brief Range of the opaque commands, sorted front to back, so a later part lies before an earlier one */
			std::uint32_t OpaqueBegin, OpaqueEnd;
			/** @brief Range of the transparent commands */
			std::uint32_t TransparentBegin, TransparentEnd;

			/** @brief Returns `true` if the part has no commands */
			bool IsEmpty() const {
				return (OpaqueBegin == OpaqueEnd && TransparentBegin == TransparentEnd);
			}
		};

		/**
		 * @brief Sorts the queues, builds batches and commits the commands
		 *
		 * No batch spans any of @p splitLayers (in ascending order), so the queue can then be drawn in parts between
		 * them (see @ref NextPart()).
		 */
		void SortAndCommit(ArrayView<const std::uint32_t> splitLayers = {});
		/** @brief Returns the empty part the first one follows, to start @ref NextPart() with */
		Part StartPart() const;
		/**
		 * @brief Returns the part that follows @p previous
		 *
		 * Contains the commands from where @p previous ends up to (but not including) @p endLayer, the rest of the
		 * queue with @ref NoSplitLayer. Only what @p previous left is searched, so the parts have to be taken in order.
		 */
		Part NextPart(const Part& previous, std::uint32_t endLayer) const;
		/** @brief Issues the render commands of the given part in order */
		void Draw(const Part& part);
#else
		void SortAndCommit();
#endif
		/** @brief Issues all the render commands in order */
		void Draw();

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
#if defined(RHI_CAP_POSTPROCESSING)
		// A queue drawn in parts counts its draw calls only once, set by the first part drawn in a frame
		bool _drawCallsCounted;
#endif

		// The queues that are drawn, the batched ones if batching is enabled
		ArrayView<RenderCommand* const> GetOpaques() const;
		ArrayView<RenderCommand* const> GetTransparents() const;
		void CountDrawCalls();
		void DrawCommands(ArrayView<RenderCommand* const> opaques, ArrayView<RenderCommand* const> transparents);
	};

}
