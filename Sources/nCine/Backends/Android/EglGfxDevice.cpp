#include "EglGfxDevice.h"
#include "../../../Main.h"
#include "AndroidJniHelper.h"
#include "AndroidApplication.h"

#include <android_native_app_glue.h> // for android_app
#include <android/configuration.h>
#if __ANDROID_API__ >= 30
#	include <android/native_window.h>
#endif

#if defined(DEATH_TARGET_ANDROID)
#include <jni.h>

extern "C"
{
	namespace nc = nCine;

	/** @brief Called by `jnicall_functions.cpp` */
	void nativeUpdateMonitors(JNIEnv* env, jclass clazz)
	{
		// This runs on the Java UI thread. Refreshing the monitors here would have to publish this thread's
		// `JNIEnv` to `AndroidJniHelper`, which the main thread is using at the same time, and would write
		// the monitor list while the main thread reads it - so only the request is recorded here.
		nc::AndroidApplication& androidApp = static_cast<nc::AndroidApplication&>(nc::theApplication());
		if (androidApp.IsInitialized()) {
			nc::Backends::EglGfxDevice::RequestMonitorsUpdate();
		}
	}
}
#endif

namespace nCine::Backends
{
	char EglGfxDevice::_monitorNames[MaxMonitors][MaxMonitorNameLength];
#if defined(DEATH_TARGET_ANDROID)
	std::atomic_bool EglGfxDevice::_monitorsUpdateRequested{false};
#endif

	EglGfxDevice::EglGfxDevice(struct android_app* state, const ContextInfo& contextInfo, const DisplayMode& displayMode)
		: IGfxDevice(WindowMode(0, 0, 0, 0, true, false, false), contextInfo, displayMode), _state(state)
	{
		updateMonitors();
		initDevice();
	}

	EglGfxDevice::~EglGfxDevice()
	{
		if (_display != EGL_NO_DISPLAY) {
			eglMakeCurrent(_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

			if (_context != EGL_NO_CONTEXT) {
				eglDestroyContext(_display, _context);
			}

			if (_surface != EGL_NO_SURFACE) {
				eglDestroySurface(_display, _surface);
			}

			eglTerminate(_display);
		}

		_display = EGL_NO_DISPLAY;
		_context = EGL_NO_CONTEXT;
		_surface = EGL_NO_SURFACE;
	}

	void EglGfxDevice::update()
	{
		eglSwapBuffers(_display, _surface);
	}

	const IGfxDevice::VideoMode& EglGfxDevice::currentVideoMode(unsigned int monitorIndex) const
	{
		if (monitorIndex >= _numMonitors) {
			monitorIndex = 0;
		}

		AndroidJniClass_Display display = AndroidJniWrap_DisplayManager::getDisplay(monitorIndex);
		AndroidJniClass_DisplayMode mode = display.getMode();
		convertVideoModeInfo(mode, _currentVideoMode);

		return _currentVideoMode;
	}

	bool EglGfxDevice::setVideoMode(unsigned int modeIndex)
	{
		const int monitorIndex = windowMonitorIndex();
		DEATH_ASSERT(monitorIndex >= 0);

		const unsigned int numVideoModes = _monitors[monitorIndex].numVideoModes;
		DEATH_ASSERT(modeIndex < numVideoModes);

#if defined(DEATH_TARGET_ANDROID) && __ANDROID_API__ >= 30
		if (modeIndex < _monitors[monitorIndex].numVideoModes) {
			const float refreshRate = _monitors[monitorIndex].videoModes[modeIndex].refreshRate;
			const int8_t compatibility = ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT;
#	if __ANDROID_API__ >= 31
			const int8_t changeFrameRateStrategy = ANATIVEWINDOW_CHANGE_FRAME_RATE_ALWAYS;
			const int result = ANativeWindow_setFrameRateWithChangeStrategy(_state->window, refreshRate, compatibility, changeFrameRateStrategy);
#	else
			const int result = ANativeWindow_setFrameRate(_state->window, refreshRate, compatibility);
#	endif
			return (result == 0);
		}
#endif
		return false;
	}

	void EglGfxDevice::createSurface()
	{
		if (_state->window != nullptr) {
			_surface = eglCreateWindowSurface(_display, _config, _state->window, nullptr);
			FATAL_ASSERT_MSG(_surface != EGL_NO_SURFACE, "eglCreateWindowSurface() returned EGL_NO_SURFACE");
		}
	}

	void EglGfxDevice::bindContext()
	{
		const EGLBoolean ret = eglMakeCurrent(_display, _surface, _surface, _context);
		FATAL_ASSERT_MSG(ret != EGL_FALSE, "eglMakeCurrent() returned EGL_FALSE");
	}

	void EglGfxDevice::unbindContext()
	{
		const EGLBoolean ret = eglMakeCurrent(_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		FATAL_ASSERT_MSG(ret != EGL_FALSE, "eglMakeCurrent() returned EGL_FALSE");
	}

	Vector2i EglGfxDevice::querySurfaceSize()
	{
		eglQuerySurface(_display, _surface, EGL_WIDTH, &_width);
		eglQuerySurface(_display, _surface, EGL_HEIGHT, &_height);
		_drawableWidth = _width;
		_drawableHeight = _height;
		return Vector2i(_width, _height);
	}

	bool EglGfxDevice::isModeSupported(struct android_app* state, const ContextInfo& contextInfo, const DisplayMode& mode)
	{
		const EGLint renderableTypeBit = (contextInfo.majorVersion == 3) ? EGL_OPENGL_ES3_BIT_KHR : EGL_OPENGL_ES2_BIT;

		const EGLint attribs[] = {
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
			EGL_RENDERABLE_TYPE, renderableTypeBit,
			EGL_BLUE_SIZE, static_cast<int>(mode.blueBits()),
			EGL_GREEN_SIZE, static_cast<int>(mode.greenBits()),
			EGL_RED_SIZE, static_cast<int>(mode.redBits()),
			EGL_ALPHA_SIZE, static_cast<int>(mode.alphaBits()),
			EGL_DEPTH_SIZE, static_cast<int>(mode.depthBits()),
			EGL_STENCIL_SIZE, static_cast<int>(mode.stencilBits()),
			EGL_NONE
		};

		EGLint format, numConfigs;
		EGLConfig config;

		EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

		eglInitialize(display, 0, 0);
		eglChooseConfig(display, attribs, &config, 1, &numConfigs);
		eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);

#if defined(DEATH_TARGET_ANDROID)
		ANativeWindow_setBuffersGeometry(state->window, 0, 0, format);
#endif

		EGLSurface surface = eglCreateWindowSurface(display, config, state->window, nullptr);

		const bool modeIsSupported = (surface != EGL_NO_SURFACE);

		if (surface != EGL_NO_SURFACE) {
			eglDestroySurface(display, surface);
		}

		eglTerminate(display);

		return modeIsSupported;
	}

#if defined(DEATH_TARGET_ANDROID)
	void EglGfxDevice::RequestMonitorsUpdate()
	{
		_monitorsUpdateRequested.store(true, std::memory_order_relaxed);
	}

	void EglGfxDevice::ProcessPendingMonitorsUpdate()
	{
		if (!_monitorsUpdateRequested.exchange(false, std::memory_order_relaxed)) {
			return;
		}

		EglGfxDevice& gfxDevice = static_cast<EglGfxDevice&>(theApplication().GetGfxDevice());
		gfxDevice.updateMonitors();
	}
#endif

	void EglGfxDevice::initDevice()
	{
		const EGLint renderableTypeBit = (_contextInfo.majorVersion == 3) ? EGL_OPENGL_ES3_BIT_KHR : EGL_OPENGL_ES2_BIT;

		const EGLint attribs[] = {
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
			EGL_RENDERABLE_TYPE, renderableTypeBit,
			EGL_BLUE_SIZE, static_cast<int>(_displayMode.blueBits()),
			EGL_GREEN_SIZE, static_cast<int>(_displayMode.greenBits()),
			EGL_RED_SIZE, static_cast<int>(_displayMode.redBits()),
			EGL_ALPHA_SIZE, static_cast<int>(_displayMode.alphaBits()),
			EGL_DEPTH_SIZE, static_cast<int>(_displayMode.depthBits()),
			EGL_STENCIL_SIZE, static_cast<int>(_displayMode.stencilBits()),
			EGL_NONE
		};

		//const EGLint glProfileMaskBit = _contextInfo.coreProfile ? EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR :
		//	EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR; // disabled

		EGLint attribList[] = {
			EGL_CONTEXT_MAJOR_VERSION_KHR, static_cast<EGLint>(_contextInfo.majorVersion),
			EGL_CONTEXT_MINOR_VERSION_KHR, static_cast<EGLint>(_contextInfo.minorVersion),
			//EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, glProfileMaskBit, // disabled
			EGL_NONE, EGL_NONE,
			EGL_NONE
		};

#if !defined(DEATH_TARGET_ANDROID) || (GL_ES_VERSION_3_0 && __ANDROID_API__ >= 21)
		if (_contextInfo.forwardCompatible || _contextInfo.debugContext) {
			attribList[4] = EGL_CONTEXT_FLAGS_KHR;
			EGLint contextFlagsMask = 0;
			contextFlagsMask |= (_contextInfo.forwardCompatible) ? EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR : 0;
			contextFlagsMask |= (_contextInfo.debugContext) ? EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR : 0;
			attribList[5] = contextFlagsMask;
		}
#endif

		EGLint format, numConfigs;

		_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

		eglInitialize(_display, 0, 0);
		eglChooseConfig(_display, attribs, &_config, 1, &numConfigs);
		eglGetConfigAttrib(_display, _config, EGL_NATIVE_VISUAL_ID, &format);

#if defined(DEATH_TARGET_ANDROID)
		ANativeWindow_setBuffersGeometry(_state->window, 0, 0, format);
#endif

		createSurface();
		_context = eglCreateContext(_display, _config, nullptr, attribList);
		FATAL_ASSERT_MSG(_context != EGL_NO_CONTEXT, "eglCreateContext() returned EGL_NO_CONTEXT");

		bindContext();
		Vector2i size = querySurfaceSize();

#if !defined(DEATH_TARGET_ANDROID)
		const EGLint swapInterval = _mode.hasVSync() ? 1 : 0;
		eglSwapInterval(_display, swapInterval);
#endif

		EGLint red, blue, green, alpha, depth, stencil, samples;
		eglGetConfigAttrib(_display, _config, EGL_RED_SIZE, &red);
		eglGetConfigAttrib(_display, _config, EGL_GREEN_SIZE, &green);
		eglGetConfigAttrib(_display, _config, EGL_BLUE_SIZE, &blue);
		eglGetConfigAttrib(_display, _config, EGL_ALPHA_SIZE, &alpha);
		eglGetConfigAttrib(_display, _config, EGL_DEPTH_SIZE, &depth);
		eglGetConfigAttrib(_display, _config, EGL_STENCIL_SIZE, &stencil);
		eglGetConfigAttrib(_display, _config, EGL_SAMPLES, &samples);

		LOGI("Surface configuration is size:{}x{}, RGBA:{}{}{}{}, depth:{}, stencil:{}, samples:{}", size.X, size.Y, red, green, blue, alpha, depth, stencil, samples);
	}

	void EglGfxDevice::updateMonitors()
	{
		const int32_t densityEnum = AConfiguration_getDensity(_state->config);
		unsigned int density = ACONFIGURATION_DENSITY_LOW;
		if (densityEnum != ACONFIGURATION_DENSITY_ANY && densityEnum != ACONFIGURATION_DENSITY_NONE) {
			density = static_cast<unsigned int>(densityEnum);
		}

		const float densityScale = density / static_cast<float>(ACONFIGURATION_DENSITY_LOW);

		AndroidJniClass_Display displays[MaxMonitors];
		const int monitorCount = AndroidJniWrap_DisplayManager::getDisplays(displays, MaxMonitors);
		DEATH_ASSERT(monitorCount >= 1);
		_numMonitors = (monitorCount < MaxMonitors) ? monitorCount : MaxMonitors;

		for (unsigned int i = 0; i < _numMonitors; i++) {
			displays[i].getName(_monitorNames[i], MaxMonitorNameLength);
			_monitors[i].name = _monitorNames[i];

			_monitors[i].position.X = 0;
			_monitors[i].position.Y = 0;
			_monitors[i].scale.X = densityScale;
			_monitors[i].scale.Y = densityScale;

			AndroidJniClass_DisplayMode modes[MaxVideoModes];
			const int modeCount = displays[i].getSupportedModes(modes, MaxVideoModes);
			_monitors[i].numVideoModes = (modeCount < MaxVideoModes) ? modeCount : MaxVideoModes;

			for (unsigned int j = 0; j < _monitors[i].numVideoModes; j++) {
				convertVideoModeInfo(modes[j], _monitors[i].videoModes[j]);
			}
		}
	}

	void EglGfxDevice::convertVideoModeInfo(const AndroidJniClass_DisplayMode& javaDisplayMode, IGfxDevice::VideoMode& videoMode) const
	{
		videoMode.width = static_cast<unsigned int>(javaDisplayMode.getPhysicalWidth());
		videoMode.height = static_cast<unsigned int>(javaDisplayMode.getPhysicalHeight());
		videoMode.refreshRate = javaDisplayMode.getRefreshRate();
		// `android.view.Display.getPixelFormat()` has been deprecated in API level 17.
		// It now always returns `android.graphics.PixelFormat.RGBA_8888`.
		videoMode.redBits = 8;
		videoMode.greenBits = 8;
		videoMode.blueBits = 8;
	}
}
