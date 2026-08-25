#include "AndroidApplication.h"
#include "../../Base/Timer.h"
#include "../../IAppEventHandler.h"
#include "../../ServiceLocator.h"
#include "EglGfxDevice.h"
#include "AndroidInputManager.h"
#include "AndroidJniHelper.h"
#include "../../tracy.h"

#include <android/input.h>

#include <IO/AndroidAssetStream.h>

using namespace Death::IO;
using namespace nCine::Backends;

extern "C"
{
	namespace nc = nCine;

	/** @brief Called by `jnicall_functions.cpp` */
	void nativeBackInvoked(JNIEnv* env, jclass clazz)
	{
		nc::AndroidApplication& androidApp = static_cast<nc::AndroidApplication&>(nc::theApplication());
		if (androidApp.IsInitialized()) {
			androidApp.HandleBackInvoked();
		}
	}

	/** @brief Called by `jnicall_functions.cpp` */
	void nativeHandleIntent(JNIEnv* env, jclass clazz, jstring action, jstring uri)
	{
		JNIEnv* oldEnv = nc::Backends::AndroidJniHelper::jniEnv;
		nc::Backends::AndroidJniHelper::jniEnv = env;

		const char* actionStr = env->GetStringUTFChars(action, nullptr);
		const char* uriStr = env->GetStringUTFChars(uri, nullptr);

		nc::AndroidApplication& androidApp = static_cast<nc::AndroidApplication&>(nc::theApplication());
		if (androidApp.IsInitialized()) {
			androidApp.HandleIntent(actionStr, uriStr);
		} else {
			LOGE("Received intent {} with \"{}\", but AndroidApplication is not initialized yet", actionStr, uriStr);
		}

		env->ReleaseStringUTFChars(action, actionStr);
		env->ReleaseStringUTFChars(uri, uriStr);

		nc::Backends::AndroidJniHelper::jniEnv = oldEnv;
	}

	/** @brief Called by `jnicall_functions.cpp` */
	void nativeTextInput(JNIEnv* env, jclass clazz, jstring text)
	{
		nc::AndroidApplication& androidApp = static_cast<nc::AndroidApplication&>(nc::theApplication());
		if (text == nullptr || !androidApp.IsInitialized()) {
			return;
		}

		// UTF-16 is used instead of `GetStringUTFChars()`, which returns modified UTF-8 with surrogate
		// pairs encoded separately
		const jsize length = env->GetStringLength(text);
		const jchar* chars = env->GetStringChars(text, nullptr);
		if (chars == nullptr) {
			return;
		}

		for (jsize i = 0; i < length; i++) {
			char32_t codePoint = chars[i];
			if (codePoint >= 0xD800 && codePoint <= 0xDBFF && i + 1 < length &&
				chars[i + 1] >= 0xDC00 && chars[i + 1] <= 0xDFFF) {
				codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (chars[i + 1] - 0xDC00);
				i++;
			}
			androidApp.HandleTextInput(codePoint);
		}

		env->ReleaseStringChars(text, chars);
	}

	/** @brief Called by `jnicall_functions.cpp` */
	void nativeKeyEvent(JNIEnv* env, jclass clazz, jint action, jint keyCode)
	{
		// `AKEY_EVENT_ACTION_DOWN` and `AKEY_EVENT_ACTION_UP` match `KeyEvent.ACTION_DOWN`/`ACTION_UP`
		nc::AndroidApplication& androidApp = static_cast<nc::AndroidApplication&>(nc::theApplication());
		if (androidApp.IsInitialized() && (action == AKEY_EVENT_ACTION_DOWN || action == AKEY_EVENT_ACTION_UP)) {
			androidApp.HandleKeyEvent(action == AKEY_EVENT_ACTION_DOWN, keyCode);
		}
	}
}

namespace nCine
{
	Application& theApplication()
	{
		static AndroidApplication instance;
		return instance;
	}

	AndroidApplication::AndroidApplication()
		: Application(), _isInitialized(false), _isBackInvoked(false), _isScreenRound(false),
			_canShowScreenKeyboard(false), _state(nullptr), _createAppEventHandler(nullptr)
	{
	}

	AndroidApplication::~AndroidApplication()
	{
	}

	void AndroidApplication::Run(struct android_app* state, CreateAppEventHandlerDelegate createAppEventHandler)
	{
		DEATH_ASSERT(state != nullptr);
		DEATH_ASSERT(createAppEventHandler != nullptr);
		AndroidApplication& app = theAndroidApplication();
		app._state = state;
		app._createAppEventHandler = createAppEventHandler;

		state->onAppCmd = AndroidApplication::ProcessCommand;

		state->onInputEvent = [](struct android_app* state, AInputEvent* event) -> std::int32_t {
			return static_cast<std::int32_t>(AndroidInputManager::parseEvent(event));
		};

		state->activity->callbacks->onContentRectChanged = [](ANativeActivity* act, const ARect* rect) {
			nc::AndroidApplication& androidApp = static_cast<nc::AndroidApplication&>(nc::theApplication());
			if (androidApp.IsInitialized()) {
				androidApp.HandleContentBoundsChanged(nc::Recti(rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top));
			}
		};

		while (!app.ShouldQuit()) {
			std::int32_t ident, events;
			struct android_poll_source* source;

			while ((ident = ALooper_pollOnce(app.ShouldSuspend() ? -1 : 0, nullptr, &events, reinterpret_cast<void**>(&source))) >= 0) {
				if (source != nullptr) {
					source->process(state, source);
				}
				if (ident == LOOPER_ID_USER) {
					AndroidInputManager::parseAccelerometerEvent();
				}
				if (state->destroyRequested) {
					LOGD("android_app->destroyRequested not equal to zero");
					app.Quit();
				}
			}

			if (app._isBackInvoked) {
				app._isBackInvoked = false;
				app._appEventHandler->OnBackInvoked();
			}

			app.ProcessDeferredInput();

			if (app.IsInitialized() && !app.ShouldSuspend()) {
				AndroidInputManager::updateJoystickConnections();
				app.Step();
			}
		}

		AndroidJniWrap_Activity::finishAndRemoveTask();
		app.Shutdown();
		exit(0);
	}

	void AndroidApplication::ProcessCommand(struct android_app* state, std::int32_t cmd)
	{
		static EglGfxDevice* eglGfxDevice = nullptr;
		// A flag to avoid resuming if the application has not been suspended first
		static bool isSuspended = false;

		switch (cmd) {
			case APP_CMD_INPUT_CHANGED: {
				LOGI("APP_CMD_INPUT_CHANGED event received");
				break;
			}
			case APP_CMD_INIT_WINDOW: {
				LOGI("APP_CMD_INIT_WINDOW event received");
				if (state->window != nullptr) {
					AndroidApplication& app = theAndroidApplication();
					if (!app.IsInitialized()) {
						app.Init();
						eglGfxDevice = &static_cast<EglGfxDevice&>(app.GetGfxDevice());
						app.Step();
					} else {
						eglGfxDevice->createSurface();
						eglGfxDevice->bindContext();
					}
				}
				break;
			}
			case APP_CMD_TERM_WINDOW: {
				LOGI("APP_CMD_TERM_WINDOW event received");
				eglGfxDevice->unbindContext();
				break;
			}
			case APP_CMD_WINDOW_RESIZED: {
				LOGI("APP_CMD_WINDOW_RESIZED event received");
				Vector2i size = eglGfxDevice->querySurfaceSize();
				theAndroidApplication().ResizeScreenViewport(size.X, size.Y);
				break;
			}
			case APP_CMD_WINDOW_REDRAW_NEEDED: {
				LOGI("APP_CMD_WINDOW_REDRAW_NEEDED event received");
				theAndroidApplication().Step();
				break;
			}
			case APP_CMD_GAINED_FOCUS: {
				LOGI("APP_CMD_GAINED_FOCUS event received");
				AndroidInputManager::enableAccelerometerSensor();
				AndroidApplication& app = theAndroidApplication();
				app.SetFocus(true);
				if (isSuspended && !app.ShouldSuspend()) {
					isSuspended = false;
					app.Resume();
				}
				break;
			}
			case APP_CMD_LOST_FOCUS: {
				LOGI("APP_CMD_LOST_FOCUS event received");
				AndroidInputManager::disableAccelerometerSensor();
				AndroidApplication& app = theAndroidApplication();
				app.SetFocus(false);
				if (!isSuspended && app.ShouldSuspend()) {
					isSuspended = true;
					app.Step();
					app.Suspend();
				}
				break;
			}
			case APP_CMD_CONFIG_CHANGED: {
				LOGI("APP_CMD_CONFIG_CHANGED event received");
				// Attaching a physical keyboard can disable the input methods providing a software one
				AndroidApplication& app = theAndroidApplication();
				app._canShowScreenKeyboard = AndroidJniWrap_InputMethodManager::isSoftInputAvailable();
				break;
			}
			case APP_CMD_LOW_MEMORY: {
				LOGW("APP_CMD_LOW_MEMORY event received");
				break;
			}
			case APP_CMD_START: {
				AndroidApplication& app = theAndroidApplication();
				if (!app.IsInitialized()) {
					app.PreInit();
					LOGI("APP_CMD_START event received");
				} else {
					LOGI("APP_CMD_START event received (resuming)");
				}
				break;
			}
			case APP_CMD_RESUME: {
				LOGI("APP_CMD_RESUME event received");
				AndroidApplication& app = theAndroidApplication();
				app._isSuspended = false;
				if (isSuspended && !app.ShouldSuspend()) {
					isSuspended = false;
					app.Resume();
				}
				break;
			}
			case APP_CMD_SAVE_STATE: {
				LOGI("APP_CMD_SAVE_STATE event received");
				break;
			}
			case APP_CMD_PAUSE: {
				LOGI("APP_CMD_PAUSE event received");
				AndroidApplication& app = theAndroidApplication();
				app._isSuspended = true;
				if (!isSuspended && app.ShouldSuspend()) {
					isSuspended = true;
					app.Suspend();
				}
				break;
			}
			case APP_CMD_STOP: {
				LOGI("APP_CMD_STOP event received");
				break;
			}
			case APP_CMD_DESTROY: {
				LOGI("APP_CMD_DESTROY event received");
				theAndroidApplication().Quit();
				break;
			}
		}
	}
	
	bool AndroidApplication::OpenUrl(StringView url)
	{
		return AndroidJniWrap_Activity::openUrl(url);
	}

	void AndroidApplication::HandleBackInvoked()
	{
		_isBackInvoked = true;
	}

	void AndroidApplication::HandleIntent(StringView action, StringView uri)
	{
		LOGI("Received intent {} with \"{}\"", action, uri);
	}

	void AndroidApplication::HandleContentBoundsChanged(Recti bounds)
	{
		LOGI("Received new content bounds: {{X: {}, Y: {}, W: {}, H: {}}}", bounds.X, bounds.Y, bounds.W, bounds.H);
	}
	
	void AndroidApplication::HandleTextInput(char32_t codePoint)
	{
		std::lock_guard<std::mutex> lock(_deferredInputLock);
		_deferredInput.push_back(DeferredInputEvent{0, codePoint, false});
	}

	void AndroidApplication::HandleKeyEvent(bool pressed, std::int32_t keyCode)
	{
		if (keyCode == 0) {
			return;
		}

		std::lock_guard<std::mutex> lock(_deferredInputLock);
		_deferredInput.push_back(DeferredInputEvent{keyCode, 0, pressed});
	}

	void AndroidApplication::ProcessDeferredInput()
	{
		SmallVector<DeferredInputEvent, 0> events;
		{
			std::lock_guard<std::mutex> lock(_deferredInputLock);
			if (!_deferredInput.empty()) {
				events = std::move(_deferredInput);
				_deferredInput.clear();
			}
		}

		for (const auto& event : events) {
			if (event.KeyCode != 0) {
				AndroidInputManager::injectKeyEvent(event.Pressed, event.KeyCode);
			} else {
				AndroidInputManager::injectTextInput(event.CodePoint);
			}
		}
	}

	bool AndroidApplication::CanShowScreenKeyboard()
	{
		return _isInitialized && _canShowScreenKeyboard;
	}

	bool AndroidApplication::IsScreenKeyboardVisible()
	{
		return _isInitialized && AndroidJniWrap_InputMethodManager::isSoftInputVisible();
	}

	bool AndroidApplication::ToggleScreenKeyboard()
	{
		if (!CanShowScreenKeyboard()) {
			return false;
		}

		return (IsScreenKeyboardVisible() ? HideScreenKeyboard() : ShowScreenKeyboard());
	}

	bool AndroidApplication::ShowScreenKeyboard()
	{
		return CanShowScreenKeyboard() && AndroidJniWrap_InputMethodManager::showSoftInput();
	}

	bool AndroidApplication::HideScreenKeyboard()
	{
		return _isInitialized && AndroidJniWrap_InputMethodManager::hideSoftInput();
	}

	void AndroidApplication::Vibrate(std::int32_t milliseconds)
	{
		if (_isInitialized) {
			AndroidJniWrap_Activity::vibrate(milliseconds);
		}
	}

	void AndroidApplication::ShowStatusBar()
	{
		if (_isInitialized) {
			AndroidJniWrap_Activity::showStatusBar();
		}
	}

	void AndroidApplication::HideStatusBar()
	{
		if (_isInitialized) {
			AndroidJniWrap_Activity::hideStatusBar();
		}
	}

	void AndroidApplication::PreInit()
	{
		_profileStartTime = TimeStamp::now();

		AndroidJniHelper::AttachJVM(_state);
		AndroidAssetStream::InitializeAssetManager(_state);

		_isScreenRound = AndroidJniWrap_Activity::isScreenRound();
		_canShowScreenKeyboard = AndroidJniWrap_InputMethodManager::isSoftInputAvailable();

		PreInitCommon(_createAppEventHandler());

#if defined(DEATH_DEBUG)
#	define INIT_MESSAGE_SUFFIX " in debug configuration"
#else
#	define INIT_MESSAGE_SUFFIX ""
#endif

		LOGI(NCINE_APP_NAME " v" NCINE_VERSION " initializing" INIT_MESSAGE_SUFFIX "...");

#if defined(DEATH_TARGET_ARM)
#	if defined(DEATH_TARGET_32BIT)
		LOGI("Running on {} {} ({}) as armeabi-v7a application", AndroidJniClass_Version::deviceBrand(), AndroidJniClass_Version::deviceModel(), AndroidJniClass_Version::deviceManufacturer());
#	else
		LOGI("Running on {} {} ({}) as arm64-v8a application", AndroidJniClass_Version::deviceBrand(), AndroidJniClass_Version::deviceModel(), AndroidJniClass_Version::deviceManufacturer());
#	endif
#elif defined(DEATH_TARGET_X86)
#	if defined(DEATH_TARGET_32BIT)
		LOGI("Running on {} {} ({}) as x86 application", AndroidJniClass_Version::deviceBrand(), AndroidJniClass_Version::deviceModel(), AndroidJniClass_Version::deviceManufacturer());
#	else
		LOGI("Running on {} {} ({}) as x64 application", AndroidJniClass_Version::deviceBrand(), AndroidJniClass_Version::deviceModel(), AndroidJniClass_Version::deviceManufacturer());
#	endif
#else
		LOGI("Running on {} {} ({})", AndroidJniClass_Version::deviceBrand(), AndroidJniClass_Version::deviceModel(), AndroidJniClass_Version::deviceManufacturer());
#endif
		LOGI("Android API version - NDK: {}, JNI: {}", __ANDROID_API__, AndroidJniHelper::SdkVersion());

		if (_isScreenRound) {
			LOGI("Using round screen layout");
		}
		if (!_canShowScreenKeyboard) {
			LOGI("No input method providing a software keyboard is enabled");
		}
	}

	void AndroidApplication::Init()
	{
		ZoneScoped;
		// Graphics device should always be created before the input manager!
		const DisplayMode displayMode32(8, 8, 8, 8, 24, 8, DisplayMode::DoubleBuffering::Enabled, DisplayMode::VSync::Disabled);
		const DisplayMode displayMode16(5, 6, 5, 0, 16, 0, DisplayMode::DoubleBuffering::Enabled, DisplayMode::VSync::Disabled);
		IGfxDevice::ContextInfo contextInfo(_appCfg);

		if (EglGfxDevice::isModeSupported(_state, contextInfo, displayMode32)) {
			_gfxDevice = std::make_unique<EglGfxDevice>(_state, contextInfo, displayMode32);
		} else if (EglGfxDevice::isModeSupported(_state, contextInfo, displayMode16)) {
			_gfxDevice = std::make_unique<EglGfxDevice>(_state, contextInfo, displayMode16);
		} else {
			LOGF("Cannot find a suitable EGL configuration, graphics device not created");
			exit(EXIT_FAILURE);
		}
		
		_inputManager = std::make_unique<AndroidInputManager>(_state);

#if defined(NCINE_PROFILING)
		_timings[(std::int32_t)Timings::PreInit] = _profileStartTime.secondsSince();
#endif

		Application::InitCommon();
		_isInitialized = true;
	}

	void AndroidApplication::Shutdown()
	{
		Application::ShutdownCommon();
		AndroidJniHelper::DetachJVM();
		_isInitialized = false;
	}
}
