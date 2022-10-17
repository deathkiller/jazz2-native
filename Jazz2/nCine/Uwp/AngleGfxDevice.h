#pragma once

#include "../Graphics/IGfxDevice.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/base.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.UI.Xaml.Controls.h>

#ifndef EGL_EGL_PROTOTYPES
#	define EGL_EGL_PROTOTYPES 1
#endif
// OpenGL ES includes
#define _USE_MATH_DEFINES

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <EGL/eglext_angle.h>

namespace winrtWAA = winrt::Windows::ApplicationModel::Activation;
namespace winrtWF = winrt::Windows::Foundation;
namespace winrtWUX = winrt::Windows::UI::Xaml;
namespace winrtWUXC = winrt::Windows::UI::Xaml::Controls;
namespace winrtWUC = winrt::Windows::UI::Core;
namespace winrtWUP = winrt::Windows::UI::Popups;

namespace nCine
{
	class UwpApplication;

	class AngleGfxDevice : public IGfxDevice
	{
		friend class UwpApplication;

	public:
		AngleGfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode, const winrtWUXC::SwapChainPanel& withVisual)
			: IGfxDevice(windowMode, glContextInfo, displayMode), _renderSurface { EGL_NO_SURFACE }, _hostVisual(withVisual)
		{
			Initialize();
			CreateRenderSurface();
		}

		~AngleGfxDevice()
		{
			DestroyRenderSurface();
			Cleanup();
		}

		inline void update() override {
			eglSwapBuffers(_eglDisplay, _renderSurface);
		}

		void setSwapInterval(int interval) override { };
		void setResolution(bool fullscreen, int width = 0, int height = 0) override { };

		void setWindowPosition(int x, int y) override { };
		void setWindowTitle(const StringView& windowTitle) override { };
		void setWindowIcon(const StringView& iconFilename) override { };

		const VideoMode& currentVideoMode(unsigned int monitorIndex) const override {
			return { };
		};

	protected:
		void setResolutionInternal(int width, int height) override { };

	private:
		void Initialize();
		void Cleanup();
		void CreateRenderSurface();
		void DestroyRenderSurface();
		void MakeCurrent();

		EGLSurface _renderSurface;
		winrtWUXC::SwapChainPanel _hostVisual;
		EGLDisplay _eglDisplay;
		EGLContext _eglContext;
		EGLConfig  _eglConfig;
	};
}
