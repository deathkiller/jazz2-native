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
		// The early boot log used the framebuffer console, which the PVR takes over now; route dbgio
		// back to the serial port (shown in the Flycast log and the dc-tool console)
		dbgio_dev_select("scif");

		// 640x480 RGB565 is the PVR's canonical output; the cable/region variant is negotiated by KOS
		vid_set_mode(DM_640x480, PM_RGB565);

		RHI::Device::InitializePvr();

		// The panel is fixed: pin every size to the display. The logical (game) resolution is driven
		// separately by the render pipeline through Device::ResizeScreenFramebuffer.
		_width = 640;
		_height = 480;
		_drawableWidth = 640;
		_drawableHeight = 480;
		_isFullscreen = true;

		_currentVideoMode.width = std::uint32_t(_width);
		_currentVideoMode.height = std::uint32_t(_height);
		_currentVideoMode.refreshRate = 60.0f;

		updateMonitors();
		initDeviceViewport();

		LOGI("Video mode initialized: {}x{} (PowerVR)", _width, _height);
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
		return _currentVideoMode;
	}

	void DcGfxDevice::setResolutionInternal(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void DcGfxDevice::updateMonitors()
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
