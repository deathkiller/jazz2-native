#include "UwpGfxDevice.h"
#include "UwpApplication.h"

#include <angle_windowsstore.h>

#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <Utf8.h>

namespace nCine
{
	UwpGfxDevice::UwpGfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode, const /*winrtWUXC::SwapChainPanel*/winrtWUC::CoreWindow& withVisual)
		: IGfxDevice(windowMode, glContextInfo, displayMode), _renderSurface { EGL_NO_SURFACE }, _hostVisual(withVisual), _sizeChanged(2)
	{
		initWindowScaling(windowMode);
		Initialize();
		CreateRenderSurface();
	}

	UwpGfxDevice::~UwpGfxDevice()
	{
		DestroyRenderSurface();
		Cleanup();
	}

	void UwpGfxDevice::Initialize()
	{
		const EGLint configAttributes[] = { EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 8, EGL_STENCIL_SIZE, 8, EGL_NONE };

		const EGLint contextAttributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

		const EGLint defaultDisplayAttributes[] = {
			// These are the default display attributes, used to request ANGLE's D3D11 renderer.
			// eglInitialize will only succeed with these attributes if the hardware supports D3D11 Feature Level 10_0+.
			EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,

			// EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE is an option that enables ANGLE to automatically call
			// the IDXGIDevice3::Trim method on behalf of the application when it gets suspended.
			// Calling IDXGIDevice3::Trim when an application is suspended is a Windows Store application certification requirement.
			EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE, EGL_TRUE,
			EGL_NONE,
		};

		const EGLint fl9_3DisplayAttributes[] = {
			// These can be used to request ANGLE's D3D11 renderer, with D3D11 Feature Level 9_3.
			// These attributes are used if the call to eglInitialize fails with the default display attributes.
			EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
			EGL_PLATFORM_ANGLE_MAX_VERSION_MAJOR_ANGLE, 9,
			EGL_PLATFORM_ANGLE_MAX_VERSION_MINOR_ANGLE, 3,
			EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE, EGL_TRUE,
			EGL_NONE,
		};

		const EGLint warpDisplayAttributes[] = {
			// These attributes can be used to request D3D11 WARP.
			// They are used if eglInitialize fails with both the default display attributes and the 9_3 display attributes.
			EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
			EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE, EGL_TRUE,
			EGL_NONE,
		};

		// eglGetPlatformDisplayEXT is an alternative to eglGetDisplay. It allows us to pass in display attributes, used to configure D3D11.
		PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
		if (!eglGetPlatformDisplayEXT) {
			throw winrt::hresult_error(E_FAIL, L"Failed to get function eglGetPlatformDisplayEXT");
		}

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
		if (_eglDisplay == EGL_NO_DISPLAY) {
			throw winrt::hresult_error(E_FAIL, L"Failed to get EGL display");
		}

		if (eglInitialize(_eglDisplay, NULL, NULL) == EGL_FALSE) {
			// This tries to initialize EGL to D3D11 Feature Level 9_3, if 10_0+ is unavailable (e.g. on some mobile devices).
			_eglDisplay = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, fl9_3DisplayAttributes);
			if (_eglDisplay == EGL_NO_DISPLAY) {
				throw winrt::hresult_error(E_FAIL, L"Failed to get EGL display");
			}

			if (eglInitialize(_eglDisplay, NULL, NULL) == EGL_FALSE) {
				// This initializes EGL to D3D11 Feature Level 11_0 on WARP, if 9_3+ is unavailable on the default GPU.
				_eglDisplay = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, warpDisplayAttributes);
				if (_eglDisplay == EGL_NO_DISPLAY) {
					throw winrt::hresult_error(E_FAIL, L"Failed to get EGL display");
				}

				if (eglInitialize(_eglDisplay, NULL, NULL) == EGL_FALSE) {
					// If all of the calls to eglInitialize returned EGL_FALSE then an error has occurred.
					throw winrt::hresult_error(E_FAIL, L"Failed to initialize EGL");
				}
			}
		}

		EGLint numConfigs = 0;

		// PFNEGLCHOOSECONFIGPROC chooseConfig = reinterpret_cast<PFNEGLCHOOSECONFIGPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
		// if (chooseConfig(_eglDisplay, configAttributes, &_eglConfig, 1, &numConfigs) == EGL_FALSE || numConfigs == 0)
		if (eglChooseConfig(_eglDisplay, configAttributes, &_eglConfig, 1, &numConfigs) == EGL_FALSE || numConfigs == 0) {
			throw winrt::hresult_error(E_FAIL, L"Failed to choose first EGLConfig");
		}

		_eglContext = eglCreateContext(_eglDisplay, _eglConfig, EGL_NO_CONTEXT, contextAttributes);
		if (_eglContext == EGL_NO_CONTEXT) {
			throw winrt::hresult_error(E_FAIL, L"Failed to create EGL context");
		}
	}

	void UwpGfxDevice::Cleanup()
	{
		if (_eglDisplay != EGL_NO_DISPLAY && _eglContext != EGL_NO_CONTEXT) {
			eglDestroyContext(_eglDisplay, _eglContext);
			_eglContext = EGL_NO_CONTEXT;
		}

		if (_eglDisplay != EGL_NO_DISPLAY) {
			eglTerminate(_eglDisplay);
			_eglDisplay = EGL_NO_DISPLAY;
		}
	}

	void UwpGfxDevice::CreateRenderSurface()
	{
		if (_renderSurface != EGL_NO_SURFACE) {
			return;
		}

		if (!_hostVisual) {
			throw winrt::hresult_error(E_INVALIDARG, L"SwapChainPanel parameter is invalid");
		}

		winrtWF::Collections::PropertySet surfaceProperties;
		surfaceProperties.Insert(EGLNativeWindowTypeProperty, _hostVisual);
		// TODO: EGL_WIDTH, EGL_HEIGHT is divided by scale factor, it looks blurry, but this call doesn't work
		//surfaceProperties.Insert(EGLRenderResolutionScaleProperty, winrtWF::PropertyValue::CreateSingle(monitors_[0].scale.X));

		_renderSurface = eglCreateWindowSurface(_eglDisplay, _eglConfig, static_cast<EGLNativeWindowType>(winrt::get_abi(surfaceProperties)), nullptr);
		if (_renderSurface == EGL_NO_SURFACE) {
			throw winrt::hresult_error(E_FAIL, L"Failed to create EGL surface");
		}
	}

	void UwpGfxDevice::DestroyRenderSurface()
	{
		if (_renderSurface != EGL_NO_SURFACE) {
			eglDestroySurface(_eglDisplay, _renderSurface);
			_renderSurface = EGL_NO_SURFACE;
		}
	}

	void UwpGfxDevice::MakeCurrent()
	{
		if (eglMakeCurrent(_eglDisplay, _renderSurface, _renderSurface, _eglContext) == EGL_FALSE) {
			throw winrt::hresult_error(E_FAIL, L"Failed to make EGLSurface current");
		}
	}

	void UwpGfxDevice::update()
	{
		eglSwapBuffers(_eglDisplay, _renderSurface);

		if (_sizeChanged > 0) {
			EGLint panelWidth = 0, panelHeight = 0;
			eglQuerySurface(_eglDisplay, _renderSurface, EGL_WIDTH, &panelWidth);
			eglQuerySurface(_eglDisplay, _renderSurface, EGL_HEIGHT, &panelHeight);
			if (panelWidth > 0 && panelHeight > 0) {
				_sizeChanged--;
				if (panelWidth != width_ || panelHeight != height_) {
					// TODO: EGL_WIDTH, EGL_HEIGHT is divided by scale factor, it looks blurry
					width_ = panelWidth;
					height_ = panelHeight;
					drawableWidth_ = width_; //* monitors_[0].scale.X;
					drawableHeight_ = height_; //* monitors_[0].scale.Y;
					theApplication().resizeScreenViewport(drawableWidth_, drawableHeight_);
				}
			}
		}
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
		auto displayInfo = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView();

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
