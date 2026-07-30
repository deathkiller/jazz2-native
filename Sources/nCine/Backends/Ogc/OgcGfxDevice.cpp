#if defined(WITH_OGC)

#include "OgcGfxDevice.h"
#include "../../Graphics/RHI/Rhi.h"
#include "../../../Main.h"

#include <cstring>
#include <malloc.h>

#include <ogc/system.h>
#include <ogc/video.h>

namespace nCine::Backends
{
	OgcGfxDevice::OgcGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode)
		: IGfxDevice(windowMode, contextInfo, displayMode), rmode_(nullptr), fbIndex_(0)
	{
		// VIDEO_Init() ran in MainApplication::Run() before any device exists
		rmode_ = VIDEO_GetPreferredMode(nullptr);
		FATAL_ASSERT_MSG(rmode_ != nullptr, "VIDEO_GetPreferredMode() failed");

		xfb_[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode_));
		xfb_[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode_));
		FATAL_ASSERT_MSG(xfb_[0] != nullptr && xfb_[1] != nullptr, "SYS_AllocateFramebuffer() failed");

		VIDEO_Configure(rmode_);
		VIDEO_SetNextFramebuffer(xfb_[0]);
		VIDEO_SetBlack(FALSE);
		VIDEO_Flush();
		VIDEO_WaitVSync();
		if (rmode_->viTVMode & VI_NON_INTERLACE) {
			VIDEO_WaitVSync();
		}

		// The panel is fixed: pin every size to the EFB-rendered area. The logical (game) resolution is
		// driven separately by the render pipeline through Device::ResizeScreenFramebuffer.
		width_ = rmode_->fbWidth;
		height_ = rmode_->efbHeight;
		drawableWidth_ = rmode_->fbWidth;
		drawableHeight_ = rmode_->efbHeight;
		isFullscreen_ = true;

		currentVideoMode_.width = std::uint32_t(width_);
		currentVideoMode_.height = std::uint32_t(height_);
		currentVideoMode_.refreshRate = ((rmode_->viTVMode >> 2) == VI_PAL ? 50.0f : 60.0f);

		updateMonitors();

		RHI::Device::InitializeGx(rmode_);

		initDeviceViewport();

		LOGI("Video mode initialized: {}x{} ({})", width_, height_,
			((rmode_->viTVMode >> 2) == VI_PAL ? "PAL" : "NTSC"));
	}

	OgcGfxDevice::~OgcGfxDevice()
	{
		// Drain the graphics pipe before the video output goes away, otherwise the GP can still be
		// processing a frame while the title is exiting
		RHI::Device::ShutdownGx();

		VIDEO_SetBlack(TRUE);
		VIDEO_Flush();
		VIDEO_WaitVSync();
	}

	void OgcGfxDevice::update()
	{
		// Finish the frame's GX draws into the back external framebuffer, then flip and pace to vsync
		fbIndex_ ^= 1;
		RHI::Device::PresentToXfb(xfb_[fbIndex_]);

		VIDEO_SetNextFramebuffer(xfb_[fbIndex_]);
		VIDEO_Flush();
		VIDEO_WaitVSync();
	}

	void OgcGfxDevice::setSwapInterval(int interval)
	{
		// The present path always paces to the TV vsync
		static_cast<void>(interval);
	}

	void OgcGfxDevice::setResolution(bool fullscreen, int width, int height)
	{
		// Fixed panel: resolution changes do not apply
		static_cast<void>(fullscreen);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void OgcGfxDevice::setWindowPosition(int x, int y)
	{
		static_cast<void>(x);
		static_cast<void>(y);
	}

	void OgcGfxDevice::setWindowSize(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void OgcGfxDevice::setWindowTitle(StringView windowTitle)
	{
		static_cast<void>(windowTitle);
	}

	void OgcGfxDevice::setWindowIcon(StringView windowIconFilename)
	{
		static_cast<void>(windowIconFilename);
	}

	const IGfxDevice::VideoMode& OgcGfxDevice::currentVideoMode(unsigned int monitorIndex) const
	{
		static_cast<void>(monitorIndex);
		return currentVideoMode_;
	}

	void OgcGfxDevice::setResolutionInternal(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void OgcGfxDevice::updateMonitors()
	{
		// One fixed "monitor" (the TV), so windowScalingFactor() and the monitor queries stay valid
		numMonitors_ = 1;
		monitors_[0].name = "TV";
		monitors_[0].position = Vector2i(0, 0);
		monitors_[0].scale = Vector2f(1.0f, 1.0f);
		monitors_[0].numVideoModes = 1;
		monitors_[0].videoModes[0] = currentVideoMode_;
	}
}

#endif
