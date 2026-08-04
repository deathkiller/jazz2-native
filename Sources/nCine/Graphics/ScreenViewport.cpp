#include "ScreenViewport.h"
#include "RenderQueue.h"
#include "RenderCommandPool.h"
#include "RenderResources.h"
#include "RenderStatistics.h"
#include "../Application.h"
#include "DisplayMode.h"
#include "RHI/Rhi.h"
#include "Camera.h"

namespace nCine
{
	ScreenViewport::ScreenViewport()
		: Viewport()
	{
		_width = theApplication().GetWidth();
		_height = theApplication().GetHeight();
		_viewportRect.Set(0, 0, _width, _height);

		const DisplayMode displayMode = theApplication().GetGfxDevice().displayMode();
		if (displayMode.depthBits() == 16) {
			_depthStencilFormat = DepthStencilFormat::Depth16;
		} else if (displayMode.depthBits() == 24) {
			_depthStencilFormat = (displayMode.stencilBits() == 8 ? DepthStencilFormat::Depth24_Stencil8 : DepthStencilFormat::Depth24);
		}
		_rootNode = &theApplication().GetRootNode();
		_type = Type::Screen;
	}

	void ScreenViewport::Resize(std::int32_t width, std::int32_t height)
	{
		if (width == _width && height == _height) {
			return;
		}

		_viewportRect.Set(0, 0, width, height);

		if (_camera != nullptr) {
			_camera->SetOrthoProjection(0.0f, float(width), 0.0f, float(height));
		}
		RenderResources::_defaultCamera->SetOrthoProjection(0.0f, float(width), 0.0f, float(height));

		_width = width;
		_height = height;
	}

	void ScreenViewport::Update()
	{
		for (std::int32_t i = std::int32_t(_chain.size()) - 1; i >= 0; i--) {
			if (_chain[i] && !_chain[i]->_stateBits.test(StateBitPositions::UpdatedBit)) {
				_chain[i]->Update();
			}
		}
		Viewport::Update();
	}

	void ScreenViewport::Visit()
	{
		for (std::int32_t i = std::int32_t(_chain.size()) - 1; i >= 0; i--) {
			if (_chain[i] && !_chain[i]->_stateBits.test(StateBitPositions::VisitedBit)) {
				_chain[i]->Visit();
			}
		}
		Viewport::Visit();
	}

	void ScreenViewport::SortAndCommitQueue()
	{
#if defined(NCINE_PROFILING)
		// Reset all rendering statistics
		RenderStatistics::Reset();
#endif

		for (std::int32_t i = std::int32_t(_chain.size()) - 1; i >= 0; i--) {
			if (_chain[i] && !_chain[i]->_stateBits.test(StateBitPositions::CommittedBit)) {
				_chain[i]->SortAndCommitQueue();
			}
		}
		Viewport::SortAndCommitQueue();

		// Now that UBOs and VBOs have been updated, they can be flushed and unmapped
		RenderResources::GetBuffersManager().FlushUnmap();
	}

	void ScreenViewport::Draw()
	{
		// Recursive calls into the chain
		Viewport::Draw(0);

		for (std::size_t i = 0; i < _chain.size(); i++) {
			if (_chain[i]) {
				_chain[i]->_renderQueue.Clear();
				_chain[i]->_stateBits.reset();
			}
		}
		_renderQueue.Clear();
		_stateBits.reset();

		RenderResources::GetBuffersManager().Remap();
		RenderResources::GetRenderCommandPool().Reset();
		RHI::Debug::Reset();
	}
}
