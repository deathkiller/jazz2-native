#pragma once

#if defined(WITH_AMIGA)

#include "../../Graphics/IGfxDevice.h"
#include "../../Primitives/Vector2.h"

#include <Containers/String.h>

struct Screen;
struct Window;
struct ScreenBuffer;
struct MsgPort;
struct BitMap;

namespace nCine
{
	class MainApplication;
}

namespace nCine::Backends
{
	/**
		@brief The `IGfxDevice` implementation for classic Amiga (Intuition + CyberGraphX/Picasso96)

		Owns the RTG screen and drives the per-frame present of the Software RHI's CPU framebuffer.
		The screen mode is chosen from the display database by the performance preset (see
		@ref AmigaPlatform::GetPerformanceClass()): a faster machine gets a larger mode, and the game
		aspect-fits its logical resolution into whatever is reported here, exactly like the fixed-panel
		consoles. Every window operation is a stub - the game owns the whole screen.

		The present is a row-by-row copy of the RHI's RGB565 buffer (native-endian, which on the 68k is
		the big-endian layout RTG calls RGB16) into the back screen buffer's locked bitmap, vertically
		flipped because the rasterizer keeps the OpenGL bottom-up convention, then a
		`ChangeScreenBuffer()` flip. A logical buffer smaller than the screen is centered with borders
		rather than scaled - no Amiga in this port's range can afford a software stretch per frame.
	*/
	class AmigaGfxDevice : public IGfxDevice
	{
	public:
		AmigaGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode);
		~AmigaGfxDevice() override;

		AmigaGfxDevice(const AmigaGfxDevice&) = delete;
		AmigaGfxDevice& operator=(const AmigaGfxDevice&) = delete;

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

		bool openDisplay(std::int32_t requestedWidth, std::int32_t requestedHeight);
		void closeDisplay();
		void presentFramebuffer();

		Screen* _screen;
		Window* _window;
		String _screenTitle;
		ScreenBuffer* _screenBuffers[2];
		MsgPort* _safePort;
		MsgPort* _dispPort;
		std::int32_t _backBuffer;
		bool _safeToWrite;
		bool _doubleBuffered;
		std::uint32_t _pixelFormat;
		bool _pixelFormatLogged;

		friend class ::nCine::MainApplication;
	};
}

#endif
