#pragma once

#include "../../Graphics/IGfxDevice.h"

#include <CommonWindows.h>

#include <winrt/base.h>
#include <winrt/Windows.UI.ViewManagement.h>

#if defined(WITH_OPENGLES)
#	if !defined(EGL_EGL_PROTOTYPES)
#		define EGL_EGL_PROTOTYPES 1
#	endif
#	define _USE_MATH_DEFINES

#	include <EGL/egl.h>
#	include <EGL/eglext.h>
#	include <EGL/eglplatform.h>
#	if defined(WITH_ANGLE)
#		include <EGL/eglext_angle.h>
#	endif
#endif

namespace winrtWF = winrt::Windows::Foundation;
namespace winrtWUC = winrt::Windows::UI::Core;
namespace winrtWUP = winrt::Windows::UI::Popups;
namespace winrtWUV = winrt::Windows::UI::ViewManagement;

namespace nCine
{
	class UwpApplication;

	class UwpGfxDevice : public IGfxDevice
	{
		friend class UwpApplication;

	public:
		UwpGfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode, const winrtWUC::CoreWindow& window);
		~UwpGfxDevice();

		void update() override;

		void setSwapInterval(int interval) override { }
		void setResolution(bool fullscreen, int width = 0, int height = 0) override;

		void setWindowPosition(int x, int y) override { };
		void setWindowSize(int width, int height) override;

		void setWindowTitle(const StringView& windowTitle) override;
		void setWindowIcon(const StringView& iconFilename) override { }

		const VideoMode& currentVideoMode(unsigned int monitorIndex) const override {
			return _currentViewMode;
		};

	protected:
		void setResolutionInternal(int width, int height) override { }

		void updateMonitors() override;

	private:
		void Initialize();
		void Cleanup();
		void MakeCurrent();

		winrtWUC::CoreWindow _window;
#if defined(WITH_OPENGLES)
		EGLSurface _renderSurface;
		EGLDisplay _eglDisplay;
		EGLContext _eglContext;
		EGLConfig  _eglConfig;
#endif
		std::int32_t _sizeChanged;
		VideoMode _currentViewMode;
	};
}
