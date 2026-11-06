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
