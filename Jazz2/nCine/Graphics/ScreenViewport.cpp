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

namespace nCine {

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	ScreenViewport::ScreenViewport()
		: Viewport()
	{
		width_ = theApplication().widthInt();
		height_ = theApplication().heightInt();
		viewportRect_.Set(0, 0, width_, height_);

		const DisplayMode displayMode = theApplication().gfxDevice().displayMode();
		if (displayMode.alphaBits() == 8)
			colorFormat_ = ColorFormat::RGBA8;

		if (displayMode.depthBits() == 16)
			depthStencilFormat_ = DepthStencilFormat::DEPTH16;
		else if (displayMode.depthBits() == 24)
			depthStencilFormat_ = (displayMode.stencilBits() == 8) ? DepthStencilFormat::DEPTH24_STENCIL8 : DepthStencilFormat::DEPTH24;

		rootNode_ = &theApplication().rootNode();
		type_ = Type::SCREEN;
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	void ScreenViewport::sortAndCommitQueue()
	{
		// Reset all rendering statistics
		RenderStatistics::reset();

		Viewport::sortAndCommitQueue();

		// Now that UBOs and VBOs have been updated, they can be flushed and unmapped
		RenderResources::buffersManager().flushUnmap();
	}

	void ScreenViewport::draw()
	{
		Viewport::draw();

		RenderResources::buffersManager().remap();
		RenderResources::renderCommandPool().reset();
		GLDebug::reset();
	}

}
