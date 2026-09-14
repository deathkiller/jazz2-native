#pragma once

#if defined(WITH_PS2)

#include "../../Graphics/IGfxDevice.h"
#include "../../Primitives/Vector2.h"

namespace nCine
{
	class MainApplication;
}

namespace nCine::Backends
{
	/**
		@brief The `IGfxDevice` implementation for the PlayStation 2 (PS2SDK)

		Owns the video mode (640x448, PSMCT16) and drives the per-frame present: the GS device flushes the
		frame's GIF packets, waits for vertical sync and flips the display buffer, so `update()` only closes
		the frame. The panel is fixed, so every window operation is a stub, exactly like the other console
		backends.

		PS2SDK does ship an SDL2 port, so the generic SDL window backend would nominally build here - but its
		shared code passes `std::int32_t*` into SDL's `int*` parameters, and `std::int32_t` is `long` on the
		Emotion Engine, so it does not compile without changing signatures every other platform depends on.
		A bespoke backend is both the cheaper and the conventional answer on this tier.
	*/
	class Ps2GfxDevice : public IGfxDevice
	{
	public:
		Ps2GfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode);
		~Ps2GfxDevice() override;

		Ps2GfxDevice(const Ps2GfxDevice&) = delete;
		Ps2GfxDevice& operator=(const Ps2GfxDevice&) = delete;

		void setSwapInterval(int interval) override;
		void setResolution(bool fullscreen, int width = 0, int height = 0) override;
		void setWindowPosition(int x, int y) override;
		void setWindowSize(int width, int height) override;
		void setWindowTitle(StringView windowTitle) override;
		void setWindowIcon(StringView windowIconFilename) override;

		const VideoMode& currentVideoMode(unsigned int monitorIndex) const override;

		/**
			@brief 4:3, which is not the 640/448 the framebuffer measures

			An NTSC field pair carries 448 lines and a television spreads them over the whole 4:3 screen, so a
			pixel of this drawable is a seventh taller than it is wide. Everything that composes a scene for
			the display - the logical view size above all - has to be told that, or it comes out stretched.
			(A PAL console runs this disc through its 60 Hz path, so there is no second geometry to answer for.)
		*/
		inline float displayAspect() const override {
			return 4.0f / 3.0f;
		}

	protected:
		void setResolutionInternal(int width, int height) override;

		void updateMonitors() override;

	private:
		void update() override;

		friend class ::nCine::MainApplication;
	};
}

#endif
