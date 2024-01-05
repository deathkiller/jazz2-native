#include "ScreenViewport.h"
#include "RenderQueue.h"
#include "RenderCommandPool.h"
#include "RenderResources.h"
#include "RenderStatistics.h"
#include "../Application.h"
#include "DisplayMode.h"
#include "GL/GLClearColor.h"
#include "GL/GLViewport.h"
#include "GL/GLDebug.h"
#include "Camera.h"

namespace nCine
{
	ScreenViewport::ScreenViewport()
		: Viewport()
	{
		width_ = theApplication().width();
		height_ = theApplication().height();
		viewportRect_.Set(0, 0, width_, height_);

		const DisplayMode displayMode = theApplication().gfxDevice().displayMode();
		if (displayMode.depthBits() == 16) {
			depthStencilFormat_ = DepthStencilFormat::Depth16;
		} else if (displayMode.depthBits() == 24) {
			depthStencilFormat_ = (displayMode.stencilBits() == 8 ? DepthStencilFormat::Depth24_Stencil8 : DepthStencilFormat::Depth24);
		}
		rootNode_ = &theApplication().rootNode();
		type_ = Type::Screen;
	}

	void ScreenViewport::resize(int width, int height)
	{
		if (width == width_ && height == height_) {
			return;
		}

		viewportRect_.Set(0, 0, width, height);

		if (camera_ != nullptr) {
			camera_->setOrthoProjection(0.0f, width, 0.0f, height);
		}
		RenderResources::defaultCamera_->setOrthoProjection(0.0f, width, 0.0f, height);

		width_ = width;
		height_ = height;
	}

	void ScreenViewport::update()
	{
		for (int i = (int)chain_.size() - 1; i >= 0; i--) {
			if (chain_[i] && !chain_[i]->stateBits_.test(StateBitPositions::UpdatedBit)) {
				chain_[i]->update();
			}
		}
		Viewport::update();
	}

	void ScreenViewport::visit()
	{
		for (int i = (int)chain_.size() - 1; i >= 0; i--) {
			if (chain_[i] && !chain_[i]->stateBits_.test(StateBitPositions::VisitedBit)) {
				chain_[i]->visit();
			}
		}
		Viewport::visit();
	}

	void ScreenViewport::sortAndCommitQueue()
	{
#if defined(NCINE_PROFILING)
		// Reset all rendering statistics
		RenderStatistics::reset();
#endif

		for (int i = (int)chain_.size() - 1; i >= 0; i--) {
			if (chain_[i] && !chain_[i]->stateBits_.test(StateBitPositions::CommittedBit)) {
				chain_[i]->sortAndCommitQueue();
			}
		}
		Viewport::sortAndCommitQueue();

		// Now that UBOs and VBOs have been updated, they can be flushed and unmapped
		RenderResources::buffersManager().flushUnmap();
	}

	void ScreenViewport::draw()
	{
		// Recursive calls into the chain
		Viewport::draw(0);

		for (unsigned int i = 0; i < chain_.size(); i++) {
			if (chain_[i]) {
				chain_[i]->renderQueue_->clear();
				chain_[i]->stateBits_.reset();
			}
		}
		renderQueue_->clear();
		stateBits_.reset();

		RenderResources::buffersManager().remap();
		RenderResources::renderCommandPool().reset();
		GLDebug::reset();
	}
}
