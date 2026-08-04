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
		: IGfxDevice(windowMode, contextInfo, displayMode), _rmode(nullptr), _fbIndex(0)
	{
		// VIDEO_Init() ran in MainApplication::Run() before any device exists
		_rmode = VIDEO_GetPreferredMode(nullptr);
		FATAL_ASSERT_MSG(_rmode != nullptr, "VIDEO_GetPreferredMode() failed");

		_xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(_rmode));
		_xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(_rmode));
		FATAL_ASSERT_MSG(_xfb[0] != nullptr && _xfb[1] != nullptr, "SYS_AllocateFramebuffer() failed");

		VIDEO_Configure(_rmode);
		VIDEO_SetNextFramebuffer(_xfb[0]);
		VIDEO_SetBlack(FALSE);
		VIDEO_Flush();
		VIDEO_WaitVSync();
		if (_rmode->viTVMode & VI_NON_INTERLACE) {
			VIDEO_WaitVSync();
		}

		// The panel is fixed: pin every size to the EFB-rendered area. The logical (game) resolution is
		// driven separately by the render pipeline through Device::ResizeScreenFramebuffer.
		_width = _rmode->fbWidth;
		_height = _rmode->efbHeight;
		_drawableWidth = _rmode->fbWidth;
		_drawableHeight = _rmode->efbHeight;
		_isFullscreen = true;

		_currentVideoMode.width = std::uint32_t(_width);
		_currentVideoMode.height = std::uint32_t(_height);
		_currentVideoMode.refreshRate = ((_rmode->viTVMode >> 2) == VI_PAL ? 50.0f : 60.0f);

		updateMonitors();

		RHI::Device::InitializeGx(_rmode);

		initDeviceViewport();

		LOGI("Video mode initialized: {}x{} ({})", _width, _height,
			((_rmode->viTVMode >> 2) == VI_PAL ? "PAL" : "NTSC"));
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
		_fbIndex ^= 1;
		RHI::Device::PresentToXfb(_xfb[_fbIndex]);

		VIDEO_SetNextFramebuffer(_xfb[_fbIndex]);
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
		return _currentVideoMode;
	}

	void OgcGfxDevice::setResolutionInternal(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void OgcGfxDevice::updateMonitors()
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
