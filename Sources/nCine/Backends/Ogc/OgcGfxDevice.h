#pragma once

#if defined(WITH_OGC)

#include "../../Graphics/IGfxDevice.h"
#include "../../Primitives/Vector2.h"

#include <gccore.h>

namespace nCine
{
	class MainApplication;
}

namespace nCine::Backends
{
	/**
		@brief The `IGfxDevice` implementation for the libogc consoles (Nintendo GameCube/Wii)

		Owns the VIDEO subsystem: picks the preferred TV mode, allocates the double-buffered external
		framebuffers and drives the per-frame present (`GxDevice::PresentToXfb` + `VIDEO_Flush` +
		vsync wait). The display panel is fixed, so every window operation is a stub, exactly like the
		Vita path of the SDL backend.
	*/
	class OgcGfxDevice : public IGfxDevice
	{
	public:
		OgcGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode);
		~OgcGfxDevice() override;

		OgcGfxDevice(const OgcGfxDevice&) = delete;
		OgcGfxDevice& operator=(const OgcGfxDevice&) = delete;

		void setSwapInterval(int interval) override;
		void setResolution(bool fullscreen, int width = 0, int height = 0) override;
		void setWindowPosition(int x, int y) override;
		void setWindowSize(int width, int height) override;
		void setWindowTitle(StringView windowTitle) override;
		void setWindowIcon(StringView windowIconFilename) override;

		const VideoMode& currentVideoMode(unsigned int monitorIndex) const override;

		/**
			@brief What the television actually spreads the 640x480 framebuffer over

			The video mode is 4:3 on both consoles, but a Wii whose system settings say the set is widescreen
			does not render anything differently - it tells the TV to stretch the same 640x480 picture across
			16:9. A scene composed for 4:3 is then 33% too wide there, which is what the game looked like on
			one. The setting lives in SYSCONF and is read once in the constructor (there is no equivalent on
			the GameCube, whose answer is always 4:3).
		*/
		inline float displayAspect() const override {
			return _displayAspect;
		}

	protected:
		void setResolutionInternal(int width, int height) override;

		void updateMonitors() override;

	private:
		GXRModeObj* _rmode;
		void* _xfb[2];
		std::int32_t _fbIndex;
		float _displayAspect;

		void update() override;

		friend class ::nCine::MainApplication;
	};
}

#endif
