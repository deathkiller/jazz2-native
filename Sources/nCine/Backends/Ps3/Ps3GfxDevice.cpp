#if defined(WITH_PS3)

#include "Ps3GfxDevice.h"
#include "Ps3InputManager.h"
#include "../../Application.h"
#include "../../Graphics/RHI/Rhi.h"
#include "../../../Main.h"

namespace nCine::Backends
{
	Ps3GfxDevice::Ps3GfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode)
		: IGfxDevice(windowMode, contextInfo, displayMode)
	{
		// Brings up the whole RSX session: the command FIFO, the video mode, the display buffers and the
		// depth buffer. The window handle is meaningless on a console (there is one display), and the size
		// is not ours to choose - the firmware publishes what the attached display accepts - so both are
		// passed as "no preference" and the negotiated mode is read back below.
		FATAL_ASSERT_MSG(RHI::Device::CreateSwapchain(nullptr, 0, 0, true),
			"Cannot initialize the RSX rendering session");

		_width = RHI::Device::GetDisplayWidth();
		_height = RHI::Device::GetDisplayHeight();
		_drawableWidth = _width;
		_drawableHeight = _height;
		_isFullscreen = true;

		_currentVideoMode.width = std::uint32_t(_width);
		_currentVideoMode.height = std::uint32_t(_height);
		_currentVideoMode.refreshRate = 60.0f;

		updateMonitors();
		initDeviceViewport();

		LOGI("Video mode initialized: {}x{} (RSX)", _width, _height);
	}

	Ps3GfxDevice::~Ps3GfxDevice()
	{
		RHI::Device::DestroySwapchain();
	}

	void Ps3GfxDevice::update()
	{
		// Flush the frame's command buffer, flip the display buffer and wait for the flip to be picked up
		RHI::Device::PresentFrame();

		// The XMB's quit request is noticed here rather than in the input manager that recorded it, so it
		// takes effect at a frame boundary: the frame that was in flight when the user pressed the PS button
		// has been presented by the line above, and the engine's own shutdown runs from here as it would for
		// any other quit - which is what releases the RSX session in order instead of leaving the firmware to
		// reclaim it.
		if (Ps3InputManager::HasExitRequested()) {
			theApplication().Quit();
		}
	}

	void Ps3GfxDevice::setSwapInterval(int interval)
	{
		// The present path always waits for vertical sync (gcmSetFlipMode(GCM_FLIP_VSYNC))
		static_cast<void>(interval);
	}

	void Ps3GfxDevice::setResolution(bool fullscreen, int width, int height)
	{
		// The display mode is negotiated with the attached display once at startup and does not change
		static_cast<void>(fullscreen);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void Ps3GfxDevice::setWindowPosition(int x, int y)
	{
		static_cast<void>(x);
		static_cast<void>(y);
	}

	void Ps3GfxDevice::setWindowSize(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void Ps3GfxDevice::setWindowTitle(StringView windowTitle)
	{
		// The title shown in the XMB comes from PARAM.SFO, which the packaging writes at build time
		static_cast<void>(windowTitle);
	}

	void Ps3GfxDevice::setWindowIcon(StringView windowIconFilename)
	{
		// Likewise ICON0.PNG, next to PARAM.SFO in the package
		static_cast<void>(windowIconFilename);
	}

	const IGfxDevice::VideoMode& Ps3GfxDevice::currentVideoMode(unsigned int monitorIndex) const
	{
		static_cast<void>(monitorIndex);
		return _currentVideoMode;
	}

	void Ps3GfxDevice::setResolutionInternal(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void Ps3GfxDevice::updateMonitors()
	{
		// One fixed "monitor" (the display the console is attached to), so windowScalingFactor() and the
		// monitor queries stay valid
		_numMonitors = 1;
		_monitors[0].name = "TV";
		_monitors[0].position = Vector2i(0, 0);
		_monitors[0].scale = Vector2f(1.0f, 1.0f);
		_monitors[0].numVideoModes = 1;
		_monitors[0].videoModes[0] = _currentVideoMode;
	}
}

#endif
