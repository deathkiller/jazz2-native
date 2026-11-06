#pragma once

#if defined(WITH_CTR)

#include "../../Graphics/IGfxDevice.h"
#include "../../Primitives/Vector2.h"

namespace nCine
{
	class MainApplication;
}

namespace nCine::Backends
{
	/**
		@brief The `IGfxDevice` implementation for the Nintendo 3DS (libctru)

		Owns the top screen (400x240, RGB565) and drives the per-frame present: the PICA device closes the
		frame's command list and queues the display transfer, so `update()` only asks it to. The panel is
		fixed, so every window operation is a stub, exactly like the other console backends. The bottom
		screen is not the game's - it keeps the boot console (see `CtrPlatform`).
	*/
	class CtrGfxDevice : public IGfxDevice
	{
	public:
		CtrGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode);
		~CtrGfxDevice() override;

		CtrGfxDevice(const CtrGfxDevice&) = delete;
		CtrGfxDevice& operator=(const CtrGfxDevice&) = delete;

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
