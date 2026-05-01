#include "RenderQueue.h"
#include "RenderBatcher.h"
#include "RenderResources.h"
#include "RenderStatistics.h"
#include "../Application.h"
#include "../Base/Algorithms.h"
#include "../tracy_opengl.h"

#if defined(DEATH_DEBUG) && defined(WITH_RHI_GL)
#	include "RHI/GL/Debug.h"
#endif

namespace nCine
{
	RenderQueue::RenderQueue()
	{
		opaqueQueue_.reserve(16);
		opaqueBatchedQueue_.reserve(16);
		transparentQueue_.reserve(16);
		transparentBatchedQueue_.reserve(16);
	}

	bool RenderQueue::IsEmpty() const
	{
		return (opaqueQueue_.empty() && transparentQueue_.empty());
	}

	void RenderQueue::AddCommand(RenderCommand* command)
	{
		// Calculating the material sorting key before adding the command to the queue
		command->CalculateMaterialSortKey();

		if (!command->GetMaterial().IsBlendingEnabled()) {
			opaqueQueue_.push_back(command);
		} else {
			transparentQueue_.push_back(command);
		}
	}

	namespace
	{
		bool descendingOrder(const RenderCommand* a, const RenderCommand* b)
		{
			return (a->GetMaterialSortKey() != b->GetMaterialSortKey())
				? a->GetMaterialSortKey() > b->GetMaterialSortKey()
				: a->GetIdSortKey() > b->GetIdSortKey();
		}

		bool ascendingOrder(const RenderCommand* a, const RenderCommand* b)
		{
			return (a->GetMaterialSortKey() != b->GetMaterialSortKey())
				? a->GetMaterialSortKey() < b->GetMaterialSortKey()
				: a->GetIdSortKey() < b->GetIdSortKey();
		}

#if defined(DEATH_DEBUG) && defined(NCINE_PROFILING)
		const char* commandTypeString(const RenderCommand& command)
		{
			switch (command.GetType()) {
				case RenderCommand::Type::Unspecified: return "unspecified";
				case RenderCommand::Type::Sprite: return "sprite";
				case RenderCommand::Type::MeshSprite: return "mesh sprite";
				case RenderCommand::Type::TileMap: return "tile map";
				case RenderCommand::Type::Particle: return "particle";
				case RenderCommand::Type::Lighting: return "lighting";
				case RenderCommand::Type::Text: return "text";
#	if defined(WITH_IMGUI)
				case RenderCommand::Type::ImGui: return "imgui";
#	endif
				default: return "unknown";
			}
		}
#endif
	}

	void RenderQueue::SortAndCommit()
	{
#if defined(DEATH_DEBUG)
		static char debugString[128];
#endif
#if defined(RHI_CAP_BATCHING)
		const bool batchingEnabled = theApplication().GetRenderingSettings().batchingEnabled;
#endif

		// Sorting the queues with the relevant orders
		sort(opaqueQueue_.begin(), opaqueQueue_.end(), descendingOrder);
		sort(transparentQueue_.begin(), transparentQueue_.end(), ascendingOrder);

#if defined(RHI_CAP_BATCHING)
		SmallVectorImpl<RenderCommand*>* opaques = (batchingEnabled ? &opaqueBatchedQueue_ : &opaqueQueue_);
		SmallVectorImpl<RenderCommand*>* transparents = (batchingEnabled ? &transparentBatchedQueue_ : &transparentQueue_);

		if (batchingEnabled) {
			ZoneScopedNC("Batching", 0x81A861);
			// Always create batches after sorting
			RenderResources::GetRenderBatcher().CreateBatches(opaqueQueue_, opaqueBatchedQueue_);
			RenderResources::GetRenderBatcher().CreateBatches(transparentQueue_, transparentBatchedQueue_);
		}
#else
		SmallVectorImpl<RenderCommand*>* opaques = &opaqueQueue_;
		SmallVectorImpl<RenderCommand*>* transparents = &transparentQueue_;
#endif

		// Avoid GPU stalls by uploading to VBOs, IBOs and UBOs before drawing
		if (!opaques->empty()) {
			ZoneScopedNC("Commit opaques", 0x81A861);
#if defined(DEATH_DEBUG) && defined(WITH_RHI_GL)
			std::size_t length = formatInto(debugString, "Commit {} opaque command(s) for viewport 0x{:x}", opaques->size(), std::uintptr_t(RenderResources::GetCurrentViewport()));
			RHI::Debug::ScopedGroup scoped({ debugString, length });
#endif
			for (RenderCommand* opaqueRenderCommand : *opaques) {
				opaqueRenderCommand->CommitAll();
			}
		}

		if (!transparents->empty()) {
			ZoneScopedNC("Commit transparents", 0x81A861);
#if defined(DEATH_DEBUG) && defined(WITH_RHI_GL)
			std::size_t length = formatInto(debugString, "Commit {} transparent command(s) for viewport 0x{:x}", transparents->size(), std::uintptr_t(RenderResources::GetCurrentViewport()));
			RHI::Debug::ScopedGroup scoped({ debugString, length });
#endif
			for (RenderCommand* transparentRenderCommand : *transparents) {
				transparentRenderCommand->CommitAll();
			}
		}
	}

	void RenderQueue::Draw()
	{
#if defined(DEATH_DEBUG) && defined(NCINE_PROFILING)
		static char debugString[128];
#endif
#if defined(RHI_CAP_BATCHING)
		const bool batchingEnabled = theApplication().GetRenderingSettings().batchingEnabled;
		SmallVectorImpl<RenderCommand*>* opaques = (batchingEnabled ? &opaqueBatchedQueue_ : &opaqueQueue_);
		SmallVectorImpl<RenderCommand*>* transparents = (batchingEnabled ? &transparentBatchedQueue_ : &transparentQueue_);
#else
		SmallVectorImpl<RenderCommand*>* opaques = &opaqueQueue_;
		SmallVectorImpl<RenderCommand*>* transparents = &transparentQueue_;
#endif

#if defined(DEATH_DEBUG) && defined(NCINE_PROFILING)
		std::uint32_t commandIndex = 0;
#endif
		// Rendering opaque nodes front to back
		for (RenderCommand* opaqueRenderCommand : *opaques) {
			TracyGpuZone("Opaque");
#if defined(DEATH_DEBUG) && defined(NCINE_PROFILING) && defined(WITH_RHI_GL)
			const std::int32_t numInstances = opaqueRenderCommand->GetInstanceCount();
			const std::int32_t batchSize = opaqueRenderCommand->GetBatchSize();
			const std::uint16_t layer = opaqueRenderCommand->GetLayer();
			const std::uint16_t visitOrder = opaqueRenderCommand->GetVisitOrder();

			std::size_t length;
			if (numInstances > 0) {
				length = formatInto(debugString, "Opaque {} ({} {} on layer {}, visit order {}, sort key 0x{:x})",
								    commandIndex, numInstances, commandTypeString(*opaqueRenderCommand), layer, visitOrder, opaqueRenderCommand->GetMaterialSortKey());
			} else if (batchSize > 0) {
				length = formatInto(debugString, "Opaque {} ({} {} on layer {}, visit order {}, sort key 0x{:x})",
								    commandIndex, batchSize, commandTypeString(*opaqueRenderCommand), layer, visitOrder, opaqueRenderCommand->GetMaterialSortKey());
			} else {
				length = formatInto(debugString, "Opaque {} ({} {} layer {}, visit order {}, sort key 0x{:x})",
								    commandIndex, commandTypeString(*opaqueRenderCommand), layer, visitOrder, opaqueRenderCommand->GetMaterialSortKey());
			}
			RHI::Debug::ScopedGroup scoped({ debugString, length });
			commandIndex++;
#endif

#if defined(NCINE_PROFILING)
			RenderStatistics::GatherStatistics(*opaqueRenderCommand);
#endif
			opaqueRenderCommand->CommitCameraTransformation();
			opaqueRenderCommand->Issue();
		}

		RHI::SetBlending(true, RHI::BlendFactor::SrcAlpha, RHI::BlendFactor::OneMinusSrcAlpha);
		RHI::SetDepthMask(false);
		// Rendering transparent nodes back to front
		for (RenderCommand* transparentRenderCommand : *transparents) {
			TracyGpuZone("Transparent");
#if defined(DEATH_DEBUG) && defined(NCINE_PROFILING) && defined(WITH_RHI_GL)
			const std::int32_t numInstances = transparentRenderCommand->GetInstanceCount();
			const std::int32_t batchSize = transparentRenderCommand->GetBatchSize();
			const std::uint16_t layer = transparentRenderCommand->GetLayer();
			const std::uint16_t visitOrder = transparentRenderCommand->GetVisitOrder();

			std::size_t length;
			if (numInstances > 0) {
				length = formatInto(debugString, "Transparent {} ({} {} on layer {}, visit order {}, sort key 0x{:x})",
								    commandIndex, numInstances, commandTypeString(*transparentRenderCommand), layer, visitOrder, transparentRenderCommand->GetMaterialSortKey());
			} else if (batchSize > 0) {
				length = formatInto(debugString, "Transparent {} ({} {} on layer {}, visit order {}, sort key 0x{:x})",
								    commandIndex, batchSize, commandTypeString(*transparentRenderCommand), layer, visitOrder, transparentRenderCommand->GetMaterialSortKey());
			} else {
				length = formatInto(debugString, "Transparent {} ({} on layer {}, visit order {}, sort key 0x{:x})",
								    commandIndex, commandTypeString(*transparentRenderCommand), layer, visitOrder, transparentRenderCommand->GetMaterialSortKey());
			}
			RHI::Debug::ScopedGroup scoped({ debugString, length });
			commandIndex++;
#endif

#if defined(NCINE_PROFILING)
			RenderStatistics::GatherStatistics(*transparentRenderCommand);
#endif
			RHI::SetBlending(true, transparentRenderCommand->GetMaterial().GetSrcBlendingFactor(),
			                       transparentRenderCommand->GetMaterial().GetDestBlendingFactor());
			transparentRenderCommand->CommitCameraTransformation();
			transparentRenderCommand->Issue();
		}
		// Depth mask must be re-enabled before exiting or clearing the depth buffer won't have any effect
		RHI::SetDepthMask(true);
		RHI::SetBlending(false, RHI::BlendFactor::One, RHI::BlendFactor::Zero);
		RHI::SetScissorTest(false, 0, 0, 0, 0);
	}

	void RenderQueue::Clear()
	{
		opaqueQueue_.clear();
		opaqueBatchedQueue_.clear();
		transparentQueue_.clear();
		transparentBatchedQueue_.clear();

		RenderResources::GetRenderBatcher().Reset();
	}
}
