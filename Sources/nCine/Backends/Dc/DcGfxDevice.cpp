#if defined(WITH_DC)

#include "DcGfxDevice.h"
#include "../../Graphics/RHI/Rhi.h"
#include "../../../Main.h"

#include <kos.h>
#include <dc/video.h>

namespace nCine::Backends
{
	DcGfxDevice::DcGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode)
		: IGfxDevice(windowMode, contextInfo, displayMode)
	{
		// 640x480 RGB565 is the PVR's canonical output; the cable/region variant is negotiated by KOS
		vid_set_mode(DM_640x480, PM_RGB565);

		RHI::Device::InitializePvr();

		// The panel is fixed: pin every size to the display. The logical (game) resolution is driven
		// separately by the render pipeline through Device::ResizeScreenFramebuffer.
		width_ = 640;
		height_ = 480;
		drawableWidth_ = 640;
		drawableHeight_ = 480;
		isFullscreen_ = true;

		currentVideoMode_.width = std::uint32_t(width_);
		currentVideoMode_.height = std::uint32_t(height_);
		currentVideoMode_.refreshRate = 60.0f;

		updateMonitors();
		initDeviceViewport();

		LOGI("Video mode initialized: {}x{} (PowerVR)", width_, height_);
	}

	DcGfxDevice::~DcGfxDevice() = default;

	void DcGfxDevice::update()
	{
		// Close the frame's TA scene (pvr_scene_finish paces the display) and drop any unconsumed
		// lighting entries, mirroring the software backend's present path
		RHI::Device::PresentFrame();
		RHI::Device::EndFrame();
	}

	void DcGfxDevice::setSwapInterval(int interval)
	{
		// The scene finish always paces to the display refresh
		static_cast<void>(interval);
	}

	void DcGfxDevice::setResolution(bool fullscreen, int width, int height)
	{
		static_cast<void>(fullscreen);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void DcGfxDevice::setWindowPosition(int x, int y)
	{
		static_cast<void>(x);
		static_cast<void>(y);
	}

	void DcGfxDevice::setWindowSize(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void DcGfxDevice::setWindowTitle(StringView windowTitle)
	{
		static_cast<void>(windowTitle);
	}

	void DcGfxDevice::setWindowIcon(StringView windowIconFilename)
	{
		static_cast<void>(windowIconFilename);
	}

	const IGfxDevice::VideoMode& DcGfxDevice::currentVideoMode(unsigned int monitorIndex) const
	{
		static_cast<void>(monitorIndex);
		return currentVideoMode_;
	}

	void DcGfxDevice::setResolutionInternal(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void DcGfxDevice::updateMonitors()
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
