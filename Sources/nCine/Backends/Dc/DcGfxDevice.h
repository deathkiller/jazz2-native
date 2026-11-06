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

		void SetSwapInterval(int interval) override;
		void SetResolution(bool fullscreen, int width = 0, int height = 0) override;
		void SetWindowPosition(int x, int y) override;
		void SetWindowSize(int width, int height) override;
		void SetWindowTitle(StringView windowTitle) override;
		void SetWindowIcon(StringView windowIconFilename) override;

		const VideoMode& GetCurrentVideoMode(unsigned int monitorIndex) const override;

	protected:
		void SetResolutionInternal(int width, int height) override;

		void UpdateMonitors() override;

	private:
		void Update() override;

		friend class ::nCine::MainApplication;
	};
}

#endif
