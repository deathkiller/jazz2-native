#if defined(WITH_PSP)

#include "PspGfxDevice.h"
#include "../../Graphics/RHI/Rhi.h"
#include "../../../Main.h"

namespace nCine::Backends
{
	PspGfxDevice::PspGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode)
		: IGfxDevice(windowMode, contextInfo, displayMode)
	{
		RHI::Device::InitializeGu();

		// The panel is fixed: pin every size to the display. The logical (game) resolution is driven
		// separately by the render pipeline through Device::ResizeScreenFramebuffer.
		width_ = 480;
		height_ = 272;
		drawableWidth_ = 480;
		drawableHeight_ = 272;
		isFullscreen_ = true;

		currentVideoMode_.width = std::uint32_t(width_);
		currentVideoMode_.height = std::uint32_t(height_);
		// The PSP panel is driven at ~59.94 Hz, which is what sceDisplayWaitVblankStart() paces to
		currentVideoMode_.refreshRate = 59.94f;

		updateMonitors();
		initDeviceViewport();

		LOGI("Video mode initialized: {}x{} (GU)", width_, height_);
	}

	PspGfxDevice::~PspGfxDevice()
	{
		// Leave the GE idle and the display off, so the exit callback's return to the firmware menu does
		// not find a list still in flight
		RHI::Device::ShutdownGu();
	}

	void PspGfxDevice::update()
	{
		// Close the frame's display list, flip the buffers, and drop any unconsumed lighting entries,
		// mirroring the Dreamcast backend's present path
		RHI::Device::PresentFrame();
		RHI::Device::EndFrame();
	}

	void PspGfxDevice::setSwapInterval(int interval)
	{
		// The present always waits for vblank
		static_cast<void>(interval);
	}

	void PspGfxDevice::setResolution(bool fullscreen, int width, int height)
	{
		static_cast<void>(fullscreen);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void PspGfxDevice::setWindowPosition(int x, int y)
	{
		static_cast<void>(x);
		static_cast<void>(y);
	}

	void PspGfxDevice::setWindowSize(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void PspGfxDevice::setWindowTitle(StringView windowTitle)
	{
		static_cast<void>(windowTitle);
	}

	void PspGfxDevice::setWindowIcon(StringView windowIconFilename)
	{
		static_cast<void>(windowIconFilename);
	}

	const IGfxDevice::VideoMode& PspGfxDevice::currentVideoMode(unsigned int monitorIndex) const
	{
		static_cast<void>(monitorIndex);
		return currentVideoMode_;
	}

	void PspGfxDevice::setResolutionInternal(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void PspGfxDevice::updateMonitors()
	{
		// One fixed "monitor" (the built-in panel), so windowScalingFactor() and the monitor queries stay valid
		numMonitors_ = 1;
		monitors_[0].name = "LCD";
		monitors_[0].position = Vector2i(0, 0);
		monitors_[0].scale = Vector2f(1.0f, 1.0f);
		monitors_[0].numVideoModes = 1;
		monitors_[0].videoModes[0] = currentVideoMode_;
	}
}

#endif
