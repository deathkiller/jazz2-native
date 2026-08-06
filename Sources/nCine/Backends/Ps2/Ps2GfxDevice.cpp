#if defined(WITH_PS2)

#include "Ps2GfxDevice.h"
#include "../../Graphics/RHI/Rhi.h"
#include "../../../Main.h"

extern "C" {
#include <kernel.h>
#include <graph.h>
}

namespace nCine::Backends
{
	Ps2GfxDevice::Ps2GfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode)
		: IGfxDevice(windowMode, contextInfo, displayMode)
	{
		// Brings up the Graphics Synthesizer: places the static video-memory regions through GsVram, sets
		// the video mode and hands `libgraph` the display buffers' addresses
		RHI::Device::InitializeGs();

		// The panel is fixed: pin every size to the display. The logical (game) resolution is driven
		// separately by the render pipeline through Device::ResizeScreenFramebuffer.
		//
		// 640x448 rather than the 640x480 the Dreamcast uses: 448 is what an NTSC field pair actually
		// carries, it is the mode PCSX2 reports the GS accepting ("NTSC 640x448"), and each buffer is a
		// whole number of GS pages high (448 = 7 x 64), which the video-memory layout depends on.
		_width = 640;
		_height = 448;
		_drawableWidth = 640;
		_drawableHeight = 448;
		_isFullscreen = true;

		_currentVideoMode.width = std::uint32_t(_width);
		_currentVideoMode.height = std::uint32_t(_height);
		_currentVideoMode.refreshRate = 60.0f;

		updateMonitors();
		initDeviceViewport();

		LOGI("Video mode initialized: {}x{} (Graphics Synthesizer)", _width, _height);
	}

	Ps2GfxDevice::~Ps2GfxDevice() = default;

	void Ps2GfxDevice::update()
	{
		// Flush the frame's remaining GIF packets, wait for vertical sync and flip; then drop any
		// unconsumed lighting entries, mirroring the other fixed-function backends' present path
		RHI::Device::PresentFrame();
		RHI::Device::EndFrame();
	}

	void Ps2GfxDevice::setSwapInterval(int interval)
	{
		// The present path always waits for vertical sync
		static_cast<void>(interval);
	}

	void Ps2GfxDevice::setResolution(bool fullscreen, int width, int height)
	{
		static_cast<void>(fullscreen);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void Ps2GfxDevice::setWindowPosition(int x, int y)
	{
		static_cast<void>(x);
		static_cast<void>(y);
	}

	void Ps2GfxDevice::setWindowSize(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void Ps2GfxDevice::setWindowTitle(StringView windowTitle)
	{
		static_cast<void>(windowTitle);
	}

	void Ps2GfxDevice::setWindowIcon(StringView windowIconFilename)
	{
		static_cast<void>(windowIconFilename);
	}

	const IGfxDevice::VideoMode& Ps2GfxDevice::currentVideoMode(unsigned int monitorIndex) const
	{
		static_cast<void>(monitorIndex);
		return _currentVideoMode;
	}

	void Ps2GfxDevice::setResolutionInternal(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void Ps2GfxDevice::updateMonitors()
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
