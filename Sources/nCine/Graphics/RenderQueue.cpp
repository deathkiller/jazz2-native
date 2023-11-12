#include "RenderQueue.h"
#include "RenderBatcher.h"
#include "RenderResources.h"
#include "RenderStatistics.h"
#include "GL/GLDebug.h"
#include "../Application.h"
#include "GL/GLScissorTest.h"
#include "GL/GLDepthTest.h"
#include "GL/GLBlending.h"
#include "../Base/Algorithms.h"
#include "../tracy_opengl.h"

namespace nCine
{
#if defined(DEATH_DEBUG)
	namespace
	{
		/// The string used to output OpenGL debug group information
		static char debugString[128];
	}
#endif

	RenderQueue::RenderQueue()
	{
		opaqueQueue_.reserve(16);
		opaqueBatchedQueue_.reserve(16);
		transparentQueue_.reserve(16);
		transparentBatchedQueue_.reserve(16);
	}

	bool RenderQueue::empty() const
	{
		return (opaqueQueue_.empty() && transparentQueue_.empty());
	}

	void RenderQueue::addCommand(RenderCommand* command)
	{
		// Calculating the material sorting key before adding the command to the queue
		command->calculateMaterialSortKey();

		if (!command->material().isBlendingEnabled()) {
			opaqueQueue_.push_back(command);
		} else {
			transparentQueue_.push_back(command);
		}
	}

	namespace
	{
		bool descendingOrder(const RenderCommand* a, const RenderCommand* b)
		{
			return (a->materialSortKey() != b->materialSortKey())
				? a->materialSortKey() > b->materialSortKey()
				: a->idSortKey() > b->idSortKey();
		}

		bool ascendingOrder(const RenderCommand* a, const RenderCommand* b)
		{
			return (a->materialSortKey() != b->materialSortKey())
				? a->materialSortKey() < b->materialSortKey()
				: a->idSortKey() < b->idSortKey();
		}

#if defined(DEATH_DEBUG) && defined(NCINE_PROFILING)
		const char* commandTypeString(const RenderCommand& command)
		{
			switch (command.type()) {
				case RenderCommand::CommandTypes::Unspecified: return "unspecified";
				case RenderCommand::CommandTypes::Sprite: return "sprite";
				case RenderCommand::CommandTypes::MeshSprite: return "mesh sprite";
				case RenderCommand::CommandTypes::TileMap: return "tile map";
				case RenderCommand::CommandTypes::Particle: return "particle";
				case RenderCommand::CommandTypes::Text: return "text";
#	if defined(WITH_IMGUI)
				case RenderCommand::CommandTypes::ImGui: return "imgui";
#	endif
				default: return "unknown";
			}
		}
#endif
	}

	void RenderQueue::sortAndCommit()
	{
		const bool batchingEnabled = theApplication().renderingSettings().batchingEnabled;

		// Sorting the queues with the relevant orders
		quicksort(opaqueQueue_.begin(), opaqueQueue_.end(), descendingOrder);
		quicksort(transparentQueue_.begin(), transparentQueue_.end(), ascendingOrder);

		SmallVectorImpl<RenderCommand*>* opaques = batchingEnabled ? &opaqueBatchedQueue_ : &opaqueQueue_;
		SmallVectorImpl<RenderCommand*>* transparents = batchingEnabled ? &transparentBatchedQueue_ : &transparentQueue_;

		if (batchingEnabled) {
			ZoneScopedNC("Batching", 0x81A861);
			// Always create batches after sorting
			RenderResources::renderBatcher().createBatches(opaqueQueue_, opaqueBatchedQueue_);
			RenderResources::renderBatcher().createBatches(transparentQueue_, transparentBatchedQueue_);
		}

		// Avoid GPU stalls by uploading to VBOs, IBOs and UBOs before drawing
		if (!opaques->empty()) {
			ZoneScopedNC("Commit opaques", 0x81A861);
#if defined(DEATH_DEBUG)
			formatString(debugString, sizeof(debugString), "Commit %u opaque command(s) for viewport 0x%lx", (uint32_t)opaques->size(), uintptr_t(RenderResources::currentViewport()));
			GLDebug::ScopedGroup scoped(debugString);
#endif
			for (RenderCommand* opaqueRenderCommand : *opaques) {
				opaqueRenderCommand->commitAll();
			}
		}

		if (!transparents->empty()) {
			ZoneScopedNC("Commit transparents", 0x81A861);
#if defined(DEATH_DEBUG)
			formatString(debugString, sizeof(debugString), "Commit %u transparent command(s) for viewport 0x%lx", (uint32_t)transparents->size(), uintptr_t(RenderResources::currentViewport()));
			GLDebug::ScopedGroup scoped(debugString);
#endif
			for (RenderCommand* transparentRenderCommand : *transparents) {
				transparentRenderCommand->commitAll();
			}
		}
	}

	void RenderQueue::draw()
	{
		const bool batchingEnabled = theApplication().renderingSettings().batchingEnabled;
		SmallVectorImpl<RenderCommand*>* opaques = batchingEnabled ? &opaqueBatchedQueue_ : &opaqueQueue_;
		SmallVectorImpl<RenderCommand*>* transparents = batchingEnabled ? &transparentBatchedQueue_ : &transparentQueue_;

#if defined(DEATH_DEBUG) && defined(NCINE_PROFILING)
		unsigned int commandIndex = 0;
#endif
		// Rendering opaque nodes front to back
		for (RenderCommand* opaqueRenderCommand : *opaques) {
			TracyGpuZone("Opaque");
#if defined(DEATH_DEBUG) && defined(NCINE_PROFILING)
			const int numInstances = opaqueRenderCommand->numInstances();
			const int batchSize = opaqueRenderCommand->batchSize();
			const uint16_t layer = opaqueRenderCommand->layer();
			const uint16_t visitOrder = opaqueRenderCommand->visitOrder();

			if (numInstances > 0) {
				formatString(debugString, sizeof(debugString), "Opaque %u (%d %s on layer %u, visit order %u, sort key %llx)",
								   commandIndex, numInstances, commandTypeString(*opaqueRenderCommand), layer, visitOrder, opaqueRenderCommand->materialSortKey());
			} else if (batchSize > 0) {
				formatString(debugString, sizeof(debugString), "Opaque %u (%d %s on layer %u, visit order %u, sort key %llx)",
								   commandIndex, batchSize, commandTypeString(*opaqueRenderCommand), layer, visitOrder, opaqueRenderCommand->materialSortKey());
			} else {
				formatString(debugString, sizeof(debugString), "Opaque %u (%s on layer %u, visit order %u, sort key %llx)",
								   commandIndex, commandTypeString(*opaqueRenderCommand), layer, visitOrder, opaqueRenderCommand->materialSortKey());
			}
			GLDebug::ScopedGroup scoped(debugString);
			commandIndex++;
#endif

#if defined(NCINE_PROFILING)
			RenderStatistics::gatherStatistics(*opaqueRenderCommand);
#endif
			opaqueRenderCommand->commitCameraTransformation();
			opaqueRenderCommand->issue();
		}

		GLBlending::enable();
		GLDepthTest::disableDepthMask();
		// Rendering transparent nodes back to front
		for (RenderCommand* transparentRenderCommand : *transparents) {
			TracyGpuZone("Transparent");
#if defined(DEATH_DEBUG) && defined(NCINE_PROFILING)
			const int numInstances = transparentRenderCommand->numInstances();
			const int batchSize = transparentRenderCommand->batchSize();
			const uint16_t layer = transparentRenderCommand->layer();
			const uint16_t visitOrder = transparentRenderCommand->visitOrder();

			if (numInstances > 0) {
				formatString(debugString, sizeof(debugString), "Transparent %u (%d %s on layer %u, visit order %u, sort key %llx)",
								   commandIndex, numInstances, commandTypeString(*transparentRenderCommand), layer, visitOrder, transparentRenderCommand->materialSortKey());
			} else if (batchSize > 0) {
				formatString(debugString, sizeof(debugString), "Transparent %u (%d %s on layer %u, visit order %u, sort key %llx)",
								   commandIndex, batchSize, commandTypeString(*transparentRenderCommand), layer, visitOrder, transparentRenderCommand->materialSortKey());
			} else {
				formatString(debugString, sizeof(debugString), "Transparent %u (%s on layer %u, visit order %u, sort key %llx)",
								   commandIndex, commandTypeString(*transparentRenderCommand), layer, visitOrder, transparentRenderCommand->materialSortKey());
			}
			GLDebug::ScopedGroup scoped(debugString);
			commandIndex++;
#endif

#if defined(NCINE_PROFILING)
			RenderStatistics::gatherStatistics(*transparentRenderCommand);
#endif
			GLBlending::setBlendFunc(transparentRenderCommand->material().srcBlendingFactor(), transparentRenderCommand->material().destBlendingFactor());
			transparentRenderCommand->commitCameraTransformation();
			transparentRenderCommand->issue();
		}
		// Depth mask has to be enabled again before exiting this method
		// or glClear(GL_DEPTH_BUFFER_BIT) won't have any effect
		GLDepthTest::enableDepthMask();
		GLBlending::disable();

		GLScissorTest::disable();
	}

	void RenderQueue::clear()
	{
		opaqueQueue_.clear();
		opaqueBatchedQueue_.clear();
		transparentQueue_.clear();
		transparentBatchedQueue_.clear();

		RenderResources::renderBatcher().reset();
	}
}
