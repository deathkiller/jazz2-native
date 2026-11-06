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

		_currentVideoMode.Width = std::uint32_t(_width);
		_currentVideoMode.Height = std::uint32_t(_height);
		_currentVideoMode.RefreshRate = 60.0f;

		UpdateMonitors();
		InitDeviceViewport();

		LOGI("Video mode initialized: {}x{} (PowerVR)", _width, _height);
	}

	DcGfxDevice::~DcGfxDevice() = default;

	void DcGfxDevice::Update()
	{
		// Close the frame's TA scene (pvr_scene_finish paces the display) and drop any unconsumed
		// lighting entries, mirroring the software backend's present path
		RHI::Device::PresentFrame();
		RHI::Device::EndFrame();
	}

	void DcGfxDevice::SetSwapInterval(int interval)
	{
		// The scene finish always paces to the display refresh
		static_cast<void>(interval);
	}

	void DcGfxDevice::SetResolution(bool fullscreen, int width, int height)
	{
		static_cast<void>(fullscreen);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void DcGfxDevice::SetWindowPosition(int x, int y)
	{
		static_cast<void>(x);
		static_cast<void>(y);
	}

	void DcGfxDevice::SetWindowSize(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void DcGfxDevice::SetWindowTitle(StringView windowTitle)
	{
		static_cast<void>(windowTitle);
	}

	void DcGfxDevice::SetWindowIcon(StringView windowIconFilename)
	{
		static_cast<void>(windowIconFilename);
	}

	const IGfxDevice::VideoMode& DcGfxDevice::GetCurrentVideoMode(unsigned int monitorIndex) const
	{
		static_cast<void>(monitorIndex);
		return _currentVideoMode;
	}

	void DcGfxDevice::SetResolutionInternal(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void DcGfxDevice::UpdateMonitors()
	{
		// One fixed "monitor" (the TV), so windowScalingFactor() and the monitor queries stay valid
		_numMonitors = 1;
		_monitors[0].Name = "TV";
		_monitors[0].Position = Vector2i(0, 0);
		_monitors[0].Scale = Vector2f(1.0f, 1.0f);
		_monitors[0].VideoModeCount = 1;
		_monitors[0].VideoModes[0] = _currentVideoMode;
	}
}

#endif
