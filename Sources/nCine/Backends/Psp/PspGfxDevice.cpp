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
		_width = 480;
		_height = 272;
		_drawableWidth = 480;
		_drawableHeight = 272;
		_isFullscreen = true;

		_currentVideoMode.width = std::uint32_t(_width);
		_currentVideoMode.height = std::uint32_t(_height);
		// The PSP panel is driven at ~59.94 Hz, which is what sceDisplayWaitVblankStart() paces to
		_currentVideoMode.refreshRate = 59.94f;

		updateMonitors();
		initDeviceViewport();

		LOGI("Video mode initialized: {}x{} (GU)", _width, _height);
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
		return _currentVideoMode;
	}

	void PspGfxDevice::setResolutionInternal(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void PspGfxDevice::updateMonitors()
	{
		// One fixed "monitor" (the built-in panel), so windowScalingFactor() and the monitor queries stay valid
		_numMonitors = 1;
		_monitors[0].name = "LCD";
		_monitors[0].position = Vector2i(0, 0);
		_monitors[0].scale = Vector2f(1.0f, 1.0f);
		_monitors[0].numVideoModes = 1;
		_monitors[0].videoModes[0] = _currentVideoMode;
	}
}

#endif
