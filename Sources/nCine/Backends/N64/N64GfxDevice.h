#pragma once

#if defined(WITH_N64)

#include "../../Graphics/IGfxDevice.h"
#include "../../Primitives/Vector2.h"

namespace nCine
{
	class MainApplication;
}

namespace nCine::Backends
{
	/**
		@brief The `IGfxDevice` implementation for the Nintendo 64 (libdragon)

		Owns the video mode (320x240, 16-bit) and drives the per-frame present: the RDP device's frame
		submission paces to the VI, so `update()` only closes the frame. The panel is fixed, so every
		window operation is a stub, exactly like the other console backends.
	*/
	class N64GfxDevice : public IGfxDevice
	{
	public:
		N64GfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode);
		~N64GfxDevice() override;

		N64GfxDevice(const N64GfxDevice&) = delete;
		N64GfxDevice& operator=(const N64GfxDevice&) = delete;

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
