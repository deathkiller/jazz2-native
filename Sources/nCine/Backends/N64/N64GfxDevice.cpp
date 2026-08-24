#if defined(WITH_N64)

#include "N64GfxDevice.h"
#include "../../Graphics/RHI/Rhi.h"
#include "../../../Main.h"

#include <libdragon.h>

namespace nCine::Backends
{
	N64GfxDevice::N64GfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode)
		: IGfxDevice(windowMode, contextInfo, displayMode)
	{
		// The early boot log used libdragon's framebuffer console, which owns the display subsystem
		// (console_close() calls display_close()); it has to be shut down before the real video mode
		// can be brought up. Traces keep flowing to stderr (ISViewer/USB log) after this.
		console_close();

		// 320x240 is what the RDP can fill at 2D workloads, and the resample filter lets the VI upscale
		// cleanly to the TV. Three buffers, not two: with two, display_get() cannot return until the
		// previous frame has been shown, which serializes the CPU against the RDP - the third is what
		// lets the CPU build a frame while the RDP draws the last one (RdpDevice's syncpoint-tracked
		// retirement keeps resource reuse safe at any buffer count). Costs 150 KB of RDRAM.
		display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);

		RHI::Device::InitializeRdp();

		// The panel is fixed: pin every size to the display. The logical (game) resolution is driven
		// separately by the render pipeline through Device::ResizeScreenFramebuffer.
		_width = 320;
		_height = 240;
		_drawableWidth = 320;
		_drawableHeight = 240;
		_isFullscreen = true;

		_currentVideoMode.width = std::uint32_t(_width);
		_currentVideoMode.height = std::uint32_t(_height);
		_currentVideoMode.refreshRate = 60.0f;

		updateMonitors();
		initDeviceViewport();

		LOGI("Video mode initialized: {}x{} (RDP)", _width, _height);
	}

	N64GfxDevice::~N64GfxDevice() = default;

	void N64GfxDevice::update()
	{
		// Close the frame's RDP work and flip the buffers (the present paces to the VI), and drop any
		// unconsumed lighting entries, mirroring the other console backends' present path
		RHI::Device::PresentFrame();
		RHI::Device::EndFrame();
	}

	void N64GfxDevice::setSwapInterval(int interval)
	{
		// The VI always paces the present to the display refresh
		static_cast<void>(interval);
	}

	void N64GfxDevice::setResolution(bool fullscreen, int width, int height)
	{
		static_cast<void>(fullscreen);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void N64GfxDevice::setWindowPosition(int x, int y)
	{
		static_cast<void>(x);
		static_cast<void>(y);
	}

	void N64GfxDevice::setWindowSize(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void N64GfxDevice::setWindowTitle(StringView windowTitle)
	{
		static_cast<void>(windowTitle);
	}

	void N64GfxDevice::setWindowIcon(StringView windowIconFilename)
	{
		static_cast<void>(windowIconFilename);
	}

	const IGfxDevice::VideoMode& N64GfxDevice::currentVideoMode(unsigned int monitorIndex) const
	{
		static_cast<void>(monitorIndex);
		return _currentVideoMode;
	}

	void N64GfxDevice::setResolutionInternal(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void N64GfxDevice::updateMonitors()
	{
		// One fixed "monitor" (the TV), so windowScalingFactor() and the monitor queries stay valid
		_numMonitors = 1;
		_monitors[0].name = "TV";
		_monitors[0].position = Vector2i(0, 0);
		_monitors[0].scale = Vector2f(1.0f, 1.0f);
		_monitors[0].numVideoModes = 1;
		_monitors[0].videoModes[0] = _currentVideoMode;
	}
}

#endif
