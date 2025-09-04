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
		width_ = theApplication().GetWidth();
		height_ = theApplication().GetHeight();
		viewportRect_.Set(0, 0, width_, height_);

		const DisplayMode displayMode = theApplication().GetGfxDevice().displayMode();
		if (displayMode.depthBits() == 16) {
			depthStencilFormat_ = DepthStencilFormat::Depth16;
		} else if (displayMode.depthBits() == 24) {
			depthStencilFormat_ = (displayMode.stencilBits() == 8 ? DepthStencilFormat::Depth24_Stencil8 : DepthStencilFormat::Depth24);
		}
		rootNode_ = &theApplication().GetRootNode();
		type_ = Type::Screen;
	}

	void ScreenViewport::Resize(std::int32_t width, std::int32_t height)
	{
		if (width == width_ && height == height_) {
			return;
		}

		viewportRect_.Set(0, 0, width, height);

		if (camera_ != nullptr) {
			camera_->SetOrthoProjection(0.0f, float(width), 0.0f, float(height));
		}
		RenderResources::defaultCamera_->SetOrthoProjection(0.0f, float(width), 0.0f, float(height));

		width_ = width;
		height_ = height;
	}

	void ScreenViewport::Update()
	{
		for (std::int32_t i = std::int32_t(chain_.size()) - 1; i >= 0; i--) {
			if (chain_[i] && !chain_[i]->stateBits_.test(StateBitPositions::UpdatedBit)) {
				chain_[i]->Update();
			}
		}
		Viewport::Update();
	}

	void ScreenViewport::Visit()
	{
		for (std::int32_t i = std::int32_t(chain_.size()) - 1; i >= 0; i--) {
			if (chain_[i] && !chain_[i]->stateBits_.test(StateBitPositions::VisitedBit)) {
				chain_[i]->Visit();
			}
		}
		Viewport::Visit();
	}

	void ScreenViewport::SortAndCommitQueue()
	{
#if defined(NCINE_PROFILING)
		// Reset all rendering statistics
		RenderStatistics::reset();
#endif

		for (std::int32_t i = std::int32_t(chain_.size()) - 1; i >= 0; i--) {
			if (chain_[i] && !chain_[i]->stateBits_.test(StateBitPositions::CommittedBit)) {
				chain_[i]->SortAndCommitQueue();
			}
		}
		Viewport::SortAndCommitQueue();

		// Now that UBOs and VBOs have been updated, they can be flushed and unmapped
		RenderResources::GetBuffersManager().flushUnmap();
	}

	void ScreenViewport::Draw()
	{
		// Recursive calls into the chain
		Viewport::Draw(0);

		for (std::size_t i = 0; i < chain_.size(); i++) {
			if (chain_[i]) {
				chain_[i]->renderQueue_.clear();
				chain_[i]->stateBits_.reset();
			}
		}
		renderQueue_.clear();
		stateBits_.reset();

		RenderResources::GetBuffersManager().remap();
		RenderResources::GetRenderCommandPool().reset();
		GLDebug::Reset();
	}
}
