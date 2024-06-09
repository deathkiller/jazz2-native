#include "UwpGfxDevice.h"
#include "UwpApplication.h"

#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Graphics.Display.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <Environment.h>
#include <Utf8.h>

#if defined(WITH_ANGLE)
#	include <angle_windowsstore.h>
#endif

namespace winrtWGD = winrt::Windows::Graphics::Display;
namespace winrtWGDC = winrt::Windows::Graphics::Display::Core;

namespace nCine
{
	UwpGfxDevice::UwpGfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode, const winrtWUC::CoreWindow& window)
		: IGfxDevice(windowMode, glContextInfo, displayMode), _window(window), _renderSurface{EGL_NO_SURFACE}, _sizeChanged(2)
	{
		updateMonitors();
		Initialize();
	}

	UwpGfxDevice::~UwpGfxDevice()
	{
		Cleanup();
	}

	void UwpGfxDevice::Initialize()
	{
#if defined(WITH_OPENGLES)
#	if defined(WITH_ANGLE)
		static const EGLint configAttributes[] = {
			EGL_RED_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8,
			EGL_ALPHA_SIZE, 8,
			EGL_DEPTH_SIZE, 8,
			EGL_STENCIL_SIZE, 8,
			EGL_NONE
		};

		static const EGLint contextAttributes[] = {
			EGL_CONTEXT_CLIENT_VERSION, 2,
			EGL_NONE
		};

		static const EGLint defaultDisplayAttributes[] = {
			// These are the default display attributes, used to request ANGLE's D3D11 renderer.
			// eglInitialize will only succeed with these attributes if the hardware supports D3D11 Feature Level 10_0+.
			EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,

			// The surface will be posted to the window using DirectComposition.
			EGL_DIRECT_COMPOSITION_ANGLE, EGL_TRUE,

			// EGL_ANGLE_DISPLAY_ALLOW_RENDER_TO_BACK_BUFFER is an optimization that can have large performance benefits on mobile devices.
			EGL_EXPERIMENTAL_PRESENT_PATH_ANGLE, EGL_EXPERIMENTAL_PRESENT_PATH_FAST_ANGLE,

			// EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE is an option that enables ANGLE to automatically call
			// the IDXGIDevice3::Trim method on behalf of the application when it gets suspended.
			// Calling IDXGIDevice3::Trim when an application is suspended is a Windows Store application certification requirement.
			EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE, EGL_TRUE,
			EGL_NONE,
		};

		static const EGLint fl9_3DisplayAttributes[] = {
			// These can be used to request ANGLE's D3D11 renderer, with D3D11 Feature Level 9_3.
			// These attributes are used if the call to eglInitialize fails with the default display attributes.
			EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
			EGL_PLATFORM_ANGLE_MAX_VERSION_MAJOR_ANGLE, 9,
			EGL_PLATFORM_ANGLE_MAX_VERSION_MINOR_ANGLE, 3,
			EGL_EXPERIMENTAL_PRESENT_PATH_ANGLE, EGL_EXPERIMENTAL_PRESENT_PATH_FAST_ANGLE,
			EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE, EGL_TRUE,
			EGL_NONE,
		};

		static const EGLint warpDisplayAttributes[] = {
			// These attributes can be used to request D3D11 WARP.
			// They are used if eglInitialize fails with both the default display attributes and the 9_3 display attributes.
			EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
			EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_DEVICE_TYPE_D3D_WARP_ANGLE,
			EGL_EXPERIMENTAL_PRESENT_PATH_ANGLE, EGL_EXPERIMENTAL_PRESENT_PATH_FAST_ANGLE,
			EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE, EGL_TRUE,
			EGL_NONE,
		};

		// eglGetPlatformDisplayEXT is an alternative to eglGetDisplay. It allows us to pass in display attributes, used to configure D3D11.
		PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
		FATAL_ASSERT_MSG(eglGetPlatformDisplayEXT != nullptr, "Failed to get function eglGetPlatformDisplayEXT()");

		//
		// To initialize the display, we make three sets of calls to eglGetPlatformDisplayEXT and eglInitialize, with varying
		// parameters passed to eglGetPlatformDisplayEXT:
		// 1) The first calls uses "defaultDisplayAttributes" as a parameter. This corresponds to D3D11 Feature Level 10_0+.
		// 2) If eglInitialize fails for step 1 (e.g. because 10_0+ isn't supported by the default GPU), then we try again
		//    using "fl9_3DisplayAttributes". This corresponds to D3D11 Feature Level 9_3.
		// 3) If eglInitialize fails for step 2 (e.g. because 9_3+ isn't supported by the default GPU), then we try again
		//    using "warpDisplayAttributes".  This corresponds to D3D11 Feature Level 11_0 on WARP, a D3D11 software rasterizer.
		//

		// This tries to initialize EGL to D3D11 Feature Level 10_0+. See above comment for details.
		_eglDisplay = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, defaultDisplayAttributes);
		FATAL_ASSERT_MSG(_eglDisplay != EGL_NO_DISPLAY, "Failed to get EGL display");

		if (eglInitialize(_eglDisplay, NULL, NULL) == EGL_FALSE) {
			// This tries to initialize EGL to D3D11 Feature Level 9_3, if 10_0+ is unavailable (e.g. on some mobile devices).
			_eglDisplay = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, fl9_3DisplayAttributes);
			FATAL_ASSERT_MSG(_eglDisplay != EGL_NO_DISPLAY, "Failed to get EGL display");

			if (eglInitialize(_eglDisplay, NULL, NULL) == EGL_FALSE) {
				// This initializes EGL to D3D11 Feature Level 11_0 on WARP, if 9_3+ is unavailable on the default GPU.
				_eglDisplay = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, warpDisplayAttributes);
				FATAL_ASSERT_MSG(_eglDisplay != EGL_NO_DISPLAY, "Failed to get EGL display");

				// If all of the calls to eglInitialize returned EGL_FALSE then an error has occurred.
				EGLBoolean result = eglInitialize(_eglDisplay, NULL, NULL);
				FATAL_ASSERT_MSG(result != EGL_FALSE, "Failed to initialize EGL");

				LOGW("Cannot initialize EGL to D3D11 Feature Level 10_0+, using Feature Level 9_3 instead");
			} else {
				LOGW("Cannot initialize EGL to D3D11 Feature Level 10_0+, using WARP instead");
			}
		}

		EGLint numConfigs = 0;
		if (eglChooseConfig(_eglDisplay, configAttributes, &_eglConfig, 1, &numConfigs) == EGL_FALSE || numConfigs == 0) {
			LOGE("Failed to choose first EGLConfig");
		}

		_eglContext = eglCreateContext(_eglDisplay, _eglConfig, EGL_NO_CONTEXT, contextAttributes);
		FATAL_ASSERT_MSG(_eglContext != EGL_NO_CONTEXT, "Failed to create EGL context");

		winrtWF::Collections::PropertySet surfaceProperties;
		surfaceProperties.Insert(EGLNativeWindowTypeProperty, _window);

		_renderSurface = eglCreateWindowSurface(_eglDisplay, _eglConfig, static_cast<EGLNativeWindowType>(winrt::get_abi(surfaceProperties)), nullptr);
		FATAL_ASSERT_MSG(_renderSurface != EGL_NO_SURFACE, "Failed to create EGL surface");
#	else
		// Generic/Mesa initialization
		static const EGLint configAttributes[] = {
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
			EGL_RED_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8,
			EGL_ALPHA_SIZE, 8,
			EGL_DEPTH_SIZE, 8,
			EGL_STENCIL_SIZE, 8,
			EGL_NONE
		};

		static const EGLint contextAttributes[] = {
			EGL_CONTEXT_CLIENT_VERSION, 2,
			EGL_NONE
		};

		static const EGLint windowAttributes[] = {
			EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
			EGL_NONE
		};

		_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
		FATAL_ASSERT_MSG(_eglDisplay != EGL_NO_DISPLAY, "Failed to get EGL display");

		if (eglInitialize(_eglDisplay, NULL, NULL) == EGL_FALSE) {
			LOGW("Cannot initialize EGL");
		}

		EGLint numConfigs = 0;
		if (eglChooseConfig(_eglDisplay, configAttributes, &_eglConfig, 1, &numConfigs) == EGL_FALSE || numConfigs == 0) {
			LOGE("Failed to choose first EGLConfig");
		}

		_eglContext = eglCreateContext(_eglDisplay, _eglConfig, EGL_NO_CONTEXT, contextAttributes);
		FATAL_ASSERT_MSG(_eglContext != EGL_NO_CONTEXT, "Failed to create EGL context");

		_renderSurface = eglCreateWindowSurface(_eglDisplay, _eglConfig, static_cast<EGLNativeWindowType>(winrt::get_abi(_window)), windowAttributes);
		FATAL_ASSERT_MSG(_renderSurface != EGL_NO_SURFACE, "Failed to create EGL surface");
#	endif
#else
#	error "For DEATH_TARGET_WINDOWS_RT, OpenGL|ES should be used instead of OpenGL"
#endif
	}

	void UwpGfxDevice::Cleanup()
	{
#if defined(WITH_OPENGLES)
		if (_renderSurface != EGL_NO_SURFACE) {
			eglDestroySurface(_eglDisplay, _renderSurface);
			_renderSurface = EGL_NO_SURFACE;
		}
		if (_eglDisplay != EGL_NO_DISPLAY) {
			if (_eglContext != EGL_NO_CONTEXT) {
				eglDestroyContext(_eglDisplay, _eglContext);
				_eglContext = EGL_NO_CONTEXT;
			}
			eglTerminate(_eglDisplay);
			_eglDisplay = EGL_NO_DISPLAY;
		}
#endif
	}

	void UwpGfxDevice::MakeCurrent()
	{
#if defined(WITH_OPENGLES)
		EGLBoolean result = eglMakeCurrent(_eglDisplay, _renderSurface, _renderSurface, _eglContext);
		FATAL_ASSERT_MSG(result != EGL_FALSE, "eglMakeCurrent() failed");

		const int interval = (displayMode_.hasVSync() ? 1 : 0);
		eglSwapInterval(_eglDisplay, interval);
#endif
	}

	void UwpGfxDevice::update()
	{
#if defined(WITH_OPENGLES)
		eglSwapBuffers(_eglDisplay, _renderSurface);

		if (_sizeChanged > 0) {
			EGLint currentWidth = 0, currentHeight = 0;
			eglQuerySurface(_eglDisplay, _renderSurface, EGL_WIDTH, &currentWidth);
			eglQuerySurface(_eglDisplay, _renderSurface, EGL_HEIGHT, &currentHeight);

			// TODO: This doesn't work correctly
			/*std::uint32_t currentWidth = 0, currentHeight = 0;
			if (Environment::CurrentDeviceType == DeviceType::Xbox) {
				const winrtWGDC::HdmiDisplayInformation hdi = winrtWGDC::HdmiDisplayInformation::GetForCurrentView();
				if (hdi) {
					winrtWGDC::HdmiDisplayMode displayMode = hdi.GetCurrentDisplayMode();
					currentWidth = displayMode.ResolutionWidthInRawPixels();
					currentHeight = displayMode.ResolutionHeightInRawPixels();
				}
			}

			if (currentWidth <= 0 || currentHeight <= 0) {
				std::int32_t scale = static_cast<std::int32_t>(winrtWGD::DisplayInformation::GetForCurrentView().ResolutionScale());
				auto bounds = _window.Bounds();
				currentWidth = (bounds.Width * scale) / 100;
				currentHeight = (bounds.Height * scale) / 100;
			}*/

			if (currentWidth > 0 && currentHeight > 0) {
				_sizeChanged--;
				if (currentWidth != width_ || currentHeight != height_) {
					width_ = currentWidth;
					height_ = currentHeight;
					drawableWidth_ = width_;
					drawableHeight_ = height_;
					theApplication().ResizeScreenViewport(drawableWidth_, drawableHeight_);
				}
			}
		}
#endif
	}

	void UwpGfxDevice::setResolution(bool fullscreen, int width, int height)
	{
		//UwpApplication::GetDispatcher().RunIdleAsync([fullscreen, width, height](auto args) {
			if (fullscreen) {
				winrtWUV::ApplicationView::GetForCurrentView().TryEnterFullScreenMode();
			} else {
				winrtWUV::ApplicationView::GetForCurrentView().ExitFullScreenMode();

				if (width > 0 && height > 0) {
					float dpi = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView().LogicalDpi();
					winrtWF::Size desiredSize = winrtWF::Size((width * DefaultDPI / dpi), (height * DefaultDPI / dpi));
					winrtWUV::ApplicationView::GetForCurrentView().TryResizeView(desiredSize);
				}
			}
		//});

		isFullscreen_ = fullscreen;
		_sizeChanged = 10;
	}

	void UwpGfxDevice::setWindowSize(int width, int height)
	{
		// This method is usually called from main thread, but it's required on UI thread
		//UwpApplication::GetDispatcher().RunIdleAsync([width, height](auto args) {
			winrtWF::Size desiredSize = winrtWF::Size(static_cast<float>(width), static_cast<float>(height));
			winrtWUV::ApplicationView::GetForCurrentView().TryResizeView(desiredSize);
		//});
	}

	void UwpGfxDevice::setWindowTitle(const StringView& windowTitle)
	{
		// TODO: Disabled for now for Windows RT, because it appends application name
		// This method is usually called from main thread, but it's required on UI thread
		//auto windowTitleW = Death::Utf8::ToUtf16(windowTitle);
		//UwpApplication::GetDispatcher().RunIdleAsync([windowTitleW = std::move(windowTitleW)](auto args) {
		//	winrtWUV::ApplicationView::GetForCurrentView().Title(winrt::hstring(windowTitleW.data(), windowTitleW.size()));
		//});
	}

	void UwpGfxDevice::updateMonitors()
	{
		auto displayInfo = winrtWGD::DisplayInformation::GetForCurrentView();

		numMonitors_ = 1;
		auto& monitor = monitors_[0];
		monitor.name = nullptr;
		monitor.position = Vector2i::Zero;

		float dpi = displayInfo.LogicalDpi();
		monitor.scale.X = dpi / DefaultDPI;
		monitor.scale.Y = dpi / DefaultDPI;

		monitor.numVideoModes = 1;
		auto& videoMode = monitor.videoModes[0];
		videoMode.width = displayInfo.ScreenWidthInRawPixels();
		videoMode.height = displayInfo.ScreenHeightInRawPixels();
	}
}
