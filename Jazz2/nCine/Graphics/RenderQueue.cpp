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

namespace nCine {

#if _DEBUG
	namespace {
		/// The string used to output OpenGL debug group information
		static char debugString[64];
	}
#endif

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	RenderQueue::RenderQueue()
	{
		opaqueQueue_.reserve(16);
		opaqueBatchedQueue_.reserve(16);
		transparentQueue_.reserve(16);
		transparentBatchedQueue_.reserve(16);
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	bool RenderQueue::empty() const
	{
		return (opaqueQueue_.empty() && transparentQueue_.empty());
	}

	void RenderQueue::addCommand(RenderCommand* command)
	{
		// Calculating the material sorting key before adding the command to the queue
		command->calculateMaterialSortKey();

		if (command->material().isBlendingEnabled() == false)
			opaqueQueue_.push_back(command);
		else
			transparentQueue_.push_back(command);
	}

	namespace {

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

		const char* commandTypeString(const RenderCommand& command)
		{
			switch (command.type()) {
				case RenderCommand::CommandTypes::UNSPECIFIED: return "unspecified";
				case RenderCommand::CommandTypes::SPRITE: return "sprite";
				case RenderCommand::CommandTypes::MESH_SPRITE: return "mesh sprite";
				case RenderCommand::CommandTypes::PARTICLE: return "particle";
				case RenderCommand::CommandTypes::TEXT: return "text";
#ifdef WITH_IMGUI
				case RenderCommand::CommandTypes::IMGUI: return "imgui";
#endif
#ifdef WITH_NUKLEAR
				case RenderCommand::CommandTypes::NUKLEAR: return "nuklear";
#endif
				default: return "unknown";
			}
		}

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
			// Always create batches after sorting
			RenderResources::renderBatcher().createBatches(opaqueQueue_, opaqueBatchedQueue_);
			RenderResources::renderBatcher().createBatches(transparentQueue_, transparentBatchedQueue_);
		}

		// Avoid GPU stalls by uploading to VBOs, IBOs and UBOs before drawing
		if (opaques->empty() == false) {
#if _DEBUG
			sprintf_s(debugString, "Commit %u opaque command(s) for viewport 0x%lx", opaques->size(), uintptr_t(RenderResources::currentViewport()));
			GLDebug::ScopedGroup scoped(debugString);
#endif
			for (RenderCommand* opaqueRenderCommand : *opaques)
				opaqueRenderCommand->commitAll();
		}

		if (transparents->empty() == false) {
#if _DEBUG
			sprintf_s(debugString, "Commit %u transparent command(s) for viewport 0x%lx", transparents->size(), uintptr_t(RenderResources::currentViewport()));
			GLDebug::ScopedGroup scoped(debugString);
#endif
			for (RenderCommand* transparentRenderCommand : *transparents)
				transparentRenderCommand->commitAll();
		}
	}

	void RenderQueue::draw()
	{
		const bool batchingEnabled = theApplication().renderingSettings().batchingEnabled;
		SmallVectorImpl<RenderCommand*>* opaques = batchingEnabled ? &opaqueBatchedQueue_ : &opaqueQueue_;
		SmallVectorImpl<RenderCommand*>* transparents = batchingEnabled ? &transparentBatchedQueue_ : &transparentQueue_;

		unsigned int commandIndex = 0;
		// Rendering opaque nodes front to back
		for (RenderCommand* opaqueRenderCommand : *opaques) {
			const int numInstances = opaqueRenderCommand->numInstances();
			const int batchSize = opaqueRenderCommand->batchSize();
			const uint16_t layer = opaqueRenderCommand->layer();
			const uint16_t visitOrder = opaqueRenderCommand->visitOrder();

#if _DEBUG
			if (numInstances > 0)
				sprintf_s(debugString, "Opaque %u (%d %s on layer %u, visit order %u)",
								   commandIndex, numInstances, commandTypeString(*opaqueRenderCommand), layer, visitOrder);
			else if (batchSize > 0)
				sprintf_s(debugString, "Opaque %u (%d %s on layer %u, visit order %u)",
								   commandIndex, batchSize, commandTypeString(*opaqueRenderCommand), layer, visitOrder);
			else
				sprintf_s(debugString, "Opaque %u (%s on layer %u, visit order %u)",
								   commandIndex, commandTypeString(*opaqueRenderCommand), layer, visitOrder);

			GLDebug::ScopedGroup scoped(debugString);
#endif
			commandIndex++;

			RenderStatistics::gatherStatistics(*opaqueRenderCommand);
			opaqueRenderCommand->commitCameraTransformation();
			opaqueRenderCommand->issue();
		}

		GLBlending::enable();
		GLDepthTest::disableDepthMask();
		// Rendering transparent nodes back to front
		for (RenderCommand* transparentRenderCommand : *transparents) {
			const int numInstances = transparentRenderCommand->numInstances();
			const int batchSize = transparentRenderCommand->batchSize();
			const uint16_t layer = transparentRenderCommand->layer();
			const uint16_t visitOrder = transparentRenderCommand->visitOrder();

#if _DEBUG
			if (numInstances > 0)
				sprintf_s(debugString, "Transparent %u (%d %s on layer %u, visit order %u)",
								   commandIndex, numInstances, commandTypeString(*transparentRenderCommand), layer, visitOrder);
			else if (batchSize > 0)
				sprintf_s(debugString, "Transparent %u (%d %s on layer %u, visit order %u)",
								   commandIndex, batchSize, commandTypeString(*transparentRenderCommand), layer, visitOrder);
			else
				sprintf_s(debugString, "Transparent %u (%s on layer %u, visit order %u)",
								   commandIndex, commandTypeString(*transparentRenderCommand), layer, visitOrder);

			GLDebug::ScopedGroup scoped(debugString);
#endif
			commandIndex++;

			RenderStatistics::gatherStatistics(*transparentRenderCommand);
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
