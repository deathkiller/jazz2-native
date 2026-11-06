#pragma once

#include "../../Graphics/IGfxDevice.h"

#include <CommonWindows.h>

#include <winrt/base.h>
#include <winrt/Windows.UI.ViewManagement.h>

#if defined(RHI_GL_PROFILE_ES)
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
}

namespace nCine::Backends
{
	/**
		@brief The UWP graphics device
		
		Renders through OpenGL ES via ANGLE/Mesa using an EGL surface bound to the UWP `CoreWindow`.
		The EGL/OpenGL ES code paths are guarded by the `RHI_GL_PROFILE_ES` build option.
	*/
	class UwpGfxDevice : public IGfxDevice
	{
		friend class UwpApplication;

	public:
		UwpGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode, const winrtWUC::CoreWindow& window);
		~UwpGfxDevice();

		void Update() override;

		void SetSwapInterval(int interval) override { }
		void SetResolution(bool fullscreen, int width = 0, int height = 0) override;

		void SetWindowPosition(int x, int y) override { };
		void SetWindowSize(int width, int height) override;

		void SetWindowTitle(StringView windowTitle) override;
		void SetWindowIcon(StringView iconFilename) override { }

		const VideoMode& GetCurrentVideoMode(unsigned int monitorIndex) const override {
			return _currentViewMode;
		};

	protected:
		void SetResolutionInternal(int width, int height) override { }

		void UpdateMonitors() override;

	private:
		void Initialize();
		void Cleanup();
		void MakeCurrent();

		winrtWUC::CoreWindow _window;
#if defined(RHI_GL_PROFILE_ES)
		EGLSurface _renderSurface;
		EGLDisplay _eglDisplay;
		EGLContext _eglContext;
		EGLConfig  _eglConfig;
#endif
		std::int32_t _sizeChanged;
		VideoMode _currentViewMode;
	};
}
