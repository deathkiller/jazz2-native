#pragma once

#if defined(WITH_DC)

#include "../../Graphics/IGfxDevice.h"
#include "../../Primitives/Vector2.h"

namespace nCine
{
	class MainApplication;
}

namespace nCine::Backends
{
	/**
		@brief The `IGfxDevice` implementation for the Sega Dreamcast (KallistiOS)

		Owns the video mode (640x480, RGB565) and drives the per-frame present: the PVR device's scene
		finish paces to the display, so `update()` only closes the frame. The panel is fixed, so every
		window operation is a stub, exactly like the other console backends.
	*/
	class DcGfxDevice : public IGfxDevice
	{
	public:
		DcGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode);
		~DcGfxDevice() override;

		DcGfxDevice(const DcGfxDevice&) = delete;
		DcGfxDevice& operator=(const DcGfxDevice&) = delete;

		void setSwapInterval(int interval) override;
		void setResolution(bool fullscreen, int width = 0, int height = 0) override;
		void setWindowPosition(int x, int y) override;
		void setWindowSize(int width, int height) override;
		void setWindowTitle(StringView windowTitle) override;
		void setWindowIcon(StringView windowIconFilename) override;

		const VideoMode& currentVideoMode(unsigned int monitorIndex) const override;

	protected:
		void setResolutionInternal(int width, int height) override;

		void updateMonitors() override;

	private:
		void update() override;

		friend class ::nCine::MainApplication;
	};
}

#endif
