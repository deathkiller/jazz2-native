#include "AngleGfxDevice.h"
#include "../Application.h"

namespace nCine
{
	void AngleGfxDevice::Initialize()
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

	void AngleGfxDevice::Cleanup()
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

	void AngleGfxDevice::CreateRenderSurface()
	{
		if (_renderSurface != EGL_NO_SURFACE) {
			return;
		}

		if (!_hostVisual) {
			throw winrt::hresult_error(E_INVALIDARG, L"SwapChainPanel parameter is invalid");
		}

		_renderSurface = eglCreateWindowSurface(_eglDisplay, _eglConfig, static_cast<EGLNativeWindowType>(winrt::get_abi(_hostVisual)), nullptr);
		if (_renderSurface == EGL_NO_SURFACE) {
			throw winrt::hresult_error(E_FAIL, L"Failed to create EGL surface");
		}
	}

	void AngleGfxDevice::DestroyRenderSurface()
	{
		if (_renderSurface != EGL_NO_SURFACE) {
			eglDestroySurface(_eglDisplay, _renderSurface);
			_renderSurface = EGL_NO_SURFACE;
		}
	}

	void AngleGfxDevice::MakeCurrent()
	{
		if (eglMakeCurrent(_eglDisplay, _renderSurface, _renderSurface, _eglContext) == EGL_FALSE) {
			throw winrt::hresult_error(E_FAIL, L"Failed to make EGLSurface current");
		}
	}

	void AngleGfxDevice::update()
	{
		eglSwapBuffers(_eglDisplay, _renderSurface);

		// TODO: Check resolution change here for now
		EGLint panelWidth = 0;
		EGLint panelHeight = 0;
		eglQuerySurface(_eglDisplay, _renderSurface, EGL_WIDTH, &panelWidth);
		eglQuerySurface(_eglDisplay, _renderSurface, EGL_HEIGHT, &panelHeight);
		if (panelWidth > 0 && panelHeight > 0 && (panelWidth != width_ || panelHeight != height_)) {
			width_ = drawableWidth_ = panelWidth;
			height_ = drawableHeight_ = panelHeight;
			theApplication().resizeScreenViewport(width_, height_);
		}
	}
}
