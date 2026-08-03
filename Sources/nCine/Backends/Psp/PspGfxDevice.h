#pragma once

#if defined(WITH_PSP)

#include "../../Graphics/IGfxDevice.h"
#include "../../Primitives/Vector2.h"

namespace nCine
{
	class MainApplication;
}

namespace nCine::Backends
{
	/**
		@brief The `IGfxDevice` implementation for the PlayStation Portable (PSPSDK)

		Owns the video mode (480x272, RGB565) and drives the per-frame present: the GU device closes the
		frame's display list, waits for the GE and flips the buffers, so `update()` only asks it to. The
		panel is fixed, so every window operation is a stub, exactly like the other console backends.
	*/
	class PspGfxDevice : public IGfxDevice
	{
	public:
		PspGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode);
		~PspGfxDevice() override;

		PspGfxDevice(const PspGfxDevice&) = delete;
		PspGfxDevice& operator=(const PspGfxDevice&) = delete;

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
