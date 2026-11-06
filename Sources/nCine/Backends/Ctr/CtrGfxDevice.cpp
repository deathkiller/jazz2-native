#if defined(WITH_CTR)

#include "CtrGfxDevice.h"
#include "CtrPlatform.h"
#include "../../Graphics/RHI/Rhi.h"
#include "../../../Main.h"

namespace nCine::Backends
{
	CtrGfxDevice::CtrGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode)
		: IGfxDevice(windowMode, contextInfo, displayMode)
	{
		FATAL_ASSERT_MSG(RHI::Device::InitializePica(), "Cannot initialize the PICA200 renderer");

		// The panel is fixed: pin every size to the top screen. The logical (game) resolution is driven
		// separately by the render pipeline through Device::ResizeScreenFramebuffer.
		_width = RHI::Device::ScreenWidth;
		_height = RHI::Device::ScreenHeight;
		_drawableWidth = _width;
		_drawableHeight = _height;
		_isFullscreen = true;

		_currentVideoMode.Width = std::uint32_t(_width);
		_currentVideoMode.Height = std::uint32_t(_height);
		// The LCD is driven at 59.83 Hz, which is what the frame's vblank sync paces to
		_currentVideoMode.RefreshRate = 59.83f;

		UpdateMonitors();
		InitDeviceViewport();

		// From here on the top screen is the game's; the bottom screen's console keeps warnings and errors only
		CtrPlatform::SetBootConsoleQuiet(true);

		LOGI("Video mode initialized: {}x{} (PICA200)", _width, _height);
	}

	CtrGfxDevice::~CtrGfxDevice()
	{
		// Leave the GPU idle before the graphics service goes away
		RHI::Device::ShutdownPica();
	}

	void CtrGfxDevice::Update()
	{
		// Close the frame's command list, queue the display transfer and drop any unconsumed lighting entries,
		// mirroring the PSP backend's present path
		RHI::Device::PresentFrame();
		RHI::Device::EndFrame();
	}

	void CtrGfxDevice::SetSwapInterval(int interval)
	{
		// The present always waits for vblank
		static_cast<void>(interval);
	}

	void CtrGfxDevice::SetResolution(bool fullscreen, int width, int height)
	{
		static_cast<void>(fullscreen);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void CtrGfxDevice::SetWindowPosition(int x, int y)
	{
		static_cast<void>(x);
		static_cast<void>(y);
	}

	void CtrGfxDevice::SetWindowSize(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void CtrGfxDevice::SetWindowTitle(StringView windowTitle)
	{
		static_cast<void>(windowTitle);
	}

	void CtrGfxDevice::SetWindowIcon(StringView windowIconFilename)
	{
		static_cast<void>(windowIconFilename);
	}

	const IGfxDevice::VideoMode& CtrGfxDevice::GetCurrentVideoMode(unsigned int monitorIndex) const
	{
		static_cast<void>(monitorIndex);
		return _currentVideoMode;
	}

	void CtrGfxDevice::SetResolutionInternal(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void CtrGfxDevice::UpdateMonitors()
	{
		// One fixed "monitor" (the top screen), so windowScalingFactor() and the monitor queries stay valid
		_numMonitors = 1;
		_monitors[0].Name = "Top screen";
		_monitors[0].Position = Vector2i(0, 0);
		_monitors[0].Scale = Vector2f(1.0f, 1.0f);
		_monitors[0].VideoModeCount = 1;
		_monitors[0].VideoModes[0] = _currentVideoMode;
	}
}

#endif
