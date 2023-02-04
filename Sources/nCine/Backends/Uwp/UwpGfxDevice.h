#pragma once

#include "../../Graphics/IGfxDevice.h"

#include <CommonWindows.h>

#ifdef GetCurrentTime
#	undef GetCurrentTime
#endif
#include <winrt/base.h>
#include <winrt/Windows.UI.ViewManagement.h>

#ifndef EGL_EGL_PROTOTYPES
#	define EGL_EGL_PROTOTYPES 1
#endif
// OpenGL ES includes
#define _USE_MATH_DEFINES

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <EGL/eglext_angle.h>

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
		UwpGfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode, const winrtWUC::CoreWindow& withVisual);
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
		void CreateRenderSurface();
		void DestroyRenderSurface();
		void MakeCurrent();

		EGLSurface _renderSurface;
		winrtWUC::CoreWindow _hostVisual;
		EGLDisplay _eglDisplay;
		EGLContext _eglContext;
		EGLConfig  _eglConfig;
		int _sizeChanged;
		VideoMode _currentViewMode;
	};
}
