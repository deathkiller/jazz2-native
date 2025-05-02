#include "AndroidApplication.h"
#include "../../Base/Timer.h"
#include "../../IAppEventHandler.h"
#include "../../ServiceLocator.h"
#include "EglGfxDevice.h"
#include "AndroidInputManager.h"
#include "AndroidJniHelper.h"
#include "../../tracy.h"

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
			LOGE("Received intent %s with \"%s\", but AndroidApplication is not initialized yet", actionStr, uriStr);
		}

		env->ReleaseStringUTFChars(action, actionStr);
		env->ReleaseStringUTFChars(uri, uriStr);

		nc::Backends::AndroidJniHelper::jniEnv = oldEnv;
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
		: Application(), isInitialized_(false), isBackInvoked_(false), isScreenRound_(false), state_(nullptr),
			createAppEventHandler_(nullptr)
	{
	}

	AndroidApplication::~AndroidApplication()
	{
	}

	void AndroidApplication::Run(struct android_app* state, CreateAppEventHandlerDelegate createAppEventHandler)
	{
		ASSERT(state != nullptr);
		ASSERT(createAppEventHandler != nullptr);
		AndroidApplication& app = theAndroidApplication();
		app.state_ = state;
		app.createAppEventHandler_ = createAppEventHandler;

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

			if (app.isBackInvoked_) {
				app.isBackInvoked_ = false;
				app.appEventHandler_->OnBackInvoked();
			}

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
				eglGfxDevice->querySurfaceSize();
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
				app.isSuspended_ = false;
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
				app.isSuspended_ = true;
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
		isBackInvoked_ = true;
	}

	void AndroidApplication::HandleIntent(StringView action, StringView uri)
	{
		LOGI("Received intent %s with \"%s\"", action.data(), uri.data());
	}

	void AndroidApplication::HandleContentBoundsChanged(Recti bounds)
	{
		LOGI("Received new content bounds: {X: %i, Y: %i, W: %i, H: %i}", bounds.X, bounds.Y, bounds.W, bounds.H);
	}
	
	bool AndroidApplication::CanShowScreenKeyboard()
	{
		return isInitialized_;
	}

	bool AndroidApplication::ToggleScreenKeyboard()
	{
		if (isInitialized_) {
			AndroidJniWrap_InputMethodManager::toggleSoftInput();
			return true;
		} else {
			return false;
		}
	}

	bool AndroidApplication::ShowScreenKeyboard()
	{
		return isInitialized_ && AndroidJniWrap_InputMethodManager::showSoftInput();
	}

	bool AndroidApplication::HideScreenKeyboard()
	{
		return isInitialized_ && AndroidJniWrap_InputMethodManager::hideSoftInput();
	}

	void AndroidApplication::PreInit()
	{
		profileStartTime_ = TimeStamp::now();

		AndroidJniHelper::AttachJVM(state_);
		AndroidAssetStream::InitializeAssetManager(state_);

		isScreenRound_ = AndroidJniWrap_Activity::isScreenRound();
		
		PreInitCommon(createAppEventHandler_());

#if defined(DEATH_TARGET_ARM)
#	if defined(DEATH_TARGET_32BIT)
		LOGI("Running on %s %s (%s) as armeabi-v7a application", AndroidJniClass_Version::deviceBrand().data(), AndroidJniClass_Version::deviceModel().data(), AndroidJniClass_Version::deviceManufacturer().data());
#	else
		LOGI("Running on %s %s (%s) as arm64-v8a application", AndroidJniClass_Version::deviceBrand().data(), AndroidJniClass_Version::deviceModel().data(), AndroidJniClass_Version::deviceManufacturer().data());
#	endif
#elif defined(DEATH_TARGET_X86)
		LOGI("Running on %s %s (%s) as x86 application", AndroidJniClass_Version::deviceBrand().data(), AndroidJniClass_Version::deviceModel().data(), AndroidJniClass_Version::deviceManufacturer().data());
#else
		LOGI("Running on %s %s (%s)", AndroidJniClass_Version::deviceBrand().data(), AndroidJniClass_Version::deviceModel().data(), AndroidJniClass_Version::deviceManufacturer().data());
#endif
		LOGI("Android API version - NDK: %d, JNI: %d", __ANDROID_API__, AndroidJniHelper::SdkVersion());

		if (isScreenRound_) {
			LOGI("Using round screen layout");
		}
	}

	void AndroidApplication::Init()
	{
		ZoneScoped;
		// Graphics device should always be created before the input manager!
		const DisplayMode displayMode32(8, 8, 8, 8, 24, 8, DisplayMode::DoubleBuffering::Enabled, DisplayMode::VSync::Disabled);
		const DisplayMode displayMode16(5, 6, 5, 0, 16, 0, DisplayMode::DoubleBuffering::Enabled, DisplayMode::VSync::Disabled);
		IGfxDevice::GLContextInfo contextInfo(appCfg_);

		if (EglGfxDevice::isModeSupported(state_, contextInfo, displayMode32)) {
			gfxDevice_ = std::make_unique<EglGfxDevice>(state_, contextInfo, displayMode32);
		} else if (EglGfxDevice::isModeSupported(state_, contextInfo, displayMode16)) {
			gfxDevice_ = std::make_unique<EglGfxDevice>(state_, contextInfo, displayMode16);
		} else {
			LOGF("Cannot find a suitable EGL configuration, graphics device not created");
			exit(EXIT_FAILURE);
		}
		
		inputManager_ = std::make_unique<AndroidInputManager>(state_);

#if defined(NCINE_PROFILING)
		timings_[(std::int32_t)Timings::PreInit] = profileStartTime_.secondsSince();
#endif

		Application::InitCommon();
		isInitialized_ = true;
	}

	void AndroidApplication::Shutdown()
	{
		Application::ShutdownCommon();
		AndroidJniHelper::DetachJVM();
		isInitialized_ = false;
	}
}
