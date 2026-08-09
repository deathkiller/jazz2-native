#pragma once

#if defined(WITH_PS3)

#include "../../Graphics/IGfxDevice.h"
#include "../../Primitives/Vector2.h"

namespace nCine
{
	class MainApplication;
}

namespace nCine::Backends
{
	/**
		@brief The `IGfxDevice` implementation for the PlayStation 3 (PSL1GHT)

		Brings up the RSX session through `RHI::Device::CreateSwapchain()` and drives the per-frame present,
		so `update()` only flips the frame and passes on whatever the XMB asked for. The panel is chosen by
		the firmware rather than by the game, so every window operation is a stub, exactly like the other
		console backends.

		Two PlayStation 3 particulars shape this backend.

		**The output resolution is negotiated, not fixed.** Where the PS2 has one 640x448 mode and the Vita
		one 960x544 panel, a PS3 is attached to whatever display the user owns and the firmware publishes the
		set of modes that display accepts. The mode is therefore picked at startup (see
		`RsxDevice::CreateSwapchain()`, which asks `videoGetResolutionAvailability()` down a preference list)
		and only reported here - `_width`/`_height` are read back from the session rather than hardcoded.
		The logical (game) resolution is a render-target size driven separately by the render pipeline, as
		on every other backend.

		**The system can ask the title to quit.** Pressing the PS button and choosing "Quit Game", or
		shutting the console down, sends `SYSUTIL_EXIT_GAME`; the firmware kills a title that does not leave
		promptly. `Ps3InputManager` records that request when it drains the callback queue, and @ref update()
		turns it into the engine's ordinary quit on the next frame - which lets the running frame finish and
		the normal shutdown path release the RSX session, rather than tearing the process down mid-frame.
	*/
	class Ps3GfxDevice : public IGfxDevice
	{
	public:
		Ps3GfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode);
		~Ps3GfxDevice() override;

		Ps3GfxDevice(const Ps3GfxDevice&) = delete;
		Ps3GfxDevice& operator=(const Ps3GfxDevice&) = delete;

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
