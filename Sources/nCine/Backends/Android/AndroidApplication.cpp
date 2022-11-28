#include "../../../Common.h"
#include "AndroidApplication.h"
#include "../../Base/Timer.h"
#include "../../IAppEventHandler.h"
#include "../../ServiceLocator.h"
#include "EglGfxDevice.h"
#include "AndroidInputManager.h"
#include "../../IO/FileSystem.h"
#include "../../IO/AssetFile.h"
#include "AndroidJniHelper.h"
#include "../../tracy.h"

namespace nc = nCine;

#if defined(NCINE_LOG)
std::unique_ptr<nCine::IFileStream> __logFile;
#endif

/// Processes the next application command
void engine_handle_cmd(struct android_app* state, int32_t cmd)
{
	nc::AndroidApplication::processCommand(state, cmd);
}

/// Parses the next input event
int32_t engine_handle_input(struct android_app* state, AInputEvent* event)
{
	return static_cast<int32_t>(nc::AndroidInputManager::parseEvent(event));
}

namespace nCine
{
	Application& theApplication()
	{
		static AndroidApplication instance;
		return instance;
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	void AndroidApplication::start(struct android_app* state, std::unique_ptr<IAppEventHandler>(*createAppEventHandler)())
	{
		ASSERT(state);
		ASSERT(createAppEventHandler);
		theAndroidApplication().state_ = state;
		theAndroidApplication().createAppEventHandler_ = createAppEventHandler;

		state->onAppCmd = engine_handle_cmd;
		state->onInputEvent = engine_handle_input;

		while (!theApplication().shouldQuit()) {
			int ident;
			int events;
			struct android_poll_source* source;

#if WITH_NUKLEAR
			NuklearAndroidInput::inputBegin();
#endif

			while ((ident = ALooper_pollAll(!theApplication().isSuspended() ? 0 : -1, nullptr, &events, reinterpret_cast<void**>(&source))) >= 0) {
				if (source != nullptr) {
					source->process(state, source);
				}
				if (ident == LOOPER_ID_USER) {
					AndroidInputManager::parseAccelerometerEvent();
				}
				if (state->destroyRequested) {
					LOGI("android_app->destroyRequested not equal to zero");
					theApplication().quit();
				}
			}

#if WITH_NUKLEAR
			NuklearAndroidInput::inputEnd();
#endif

			if (theAndroidApplication().isInitialized() && !theApplication().shouldSuspend()) {
				AndroidInputManager::updateJoystickConnections();
				theApplication().step();
			}
		}

		AndroidJniWrap_Activity::finishAndRemoveTask();
		theAndroidApplication().shutdown();
		exit(0);
	}

	void AndroidApplication::processCommand(struct android_app* state, int32_t cmd)
	{
		static EglGfxDevice* eglGfxDevice = nullptr;
		// A flag to avoid resuming if the application has not been suspended first
		static bool isPaused = false;

		switch (cmd) {
			case APP_CMD_INPUT_CHANGED:
				LOGW("APP_CMD_INPUT_CHANGED event received (not handled)");
				break;

			case APP_CMD_INIT_WINDOW:
				LOGI("APP_CMD_INIT_WINDOW event received");
				if (state->window != nullptr) {
					if (theAndroidApplication().isInitialized() == false) {
						theAndroidApplication().init();
						eglGfxDevice = &static_cast<EglGfxDevice&>(theApplication().gfxDevice());
						theApplication().step();
					} else {
						eglGfxDevice->createSurface();
						eglGfxDevice->bindContext();
					}
				}
				break;
			case APP_CMD_TERM_WINDOW:
				LOGI("APP_CMD_TERM_WINDOW event received");
				eglGfxDevice->unbindContext();
				break;
			case APP_CMD_WINDOW_RESIZED:
				LOGI("APP_CMD_WINDOW_RESIZED event received");
				eglGfxDevice->querySurfaceSize();
				break;
			case APP_CMD_WINDOW_REDRAW_NEEDED:
				LOGI("APP_CMD_WINDOW_REDRAW_NEEDED event received");
				theApplication().step();
				break;

			case APP_CMD_GAINED_FOCUS:
				LOGI("APP_CMD_GAINED_FOCUS event received");
				AndroidInputManager::enableAccelerometerSensor();
				theApplication().setFocus(true);
				break;
			case APP_CMD_LOST_FOCUS:
				LOGI("APP_CMD_LOST_FOCUS event received");
				AndroidInputManager::disableAccelerometerSensor();
				theApplication().setFocus(false);
				theApplication().step();
				break;

			case APP_CMD_CONFIG_CHANGED:
				LOGW("APP_CMD_CONFIG_CHANGED event received (not handled)");
				break;

			case APP_CMD_LOW_MEMORY:
				LOGW("APP_CMD_LOW_MEMORY event received (not handled)");
				break;

			case APP_CMD_START:
				if (theAndroidApplication().isInitialized() == false) {
					theAndroidApplication().preInit();
					LOGI("APP_CMD_START event received (first start)");
				}
				LOGI("APP_CMD_START event received");
				break;
			case APP_CMD_RESUME:
				LOGW("APP_CMD_RESUME event received");
				if (isPaused)
					theAndroidApplication().resume();
				isPaused = false;
				break;
			case APP_CMD_SAVE_STATE:
				LOGW("APP_CMD_SAVE_STATE event received (not handled)");
				break;
			case APP_CMD_PAUSE:
				LOGW("APP_CMD_PAUSE event received");
				theAndroidApplication().suspend();
				isPaused = true;
				break;
			case APP_CMD_STOP:
				LOGW("APP_CMD_STOP event received (not handled)");
				break;

			case APP_CMD_DESTROY:
				LOGI("APP_CMD_DESTROY event received");
				theApplication().quit();
				break;
		}
	}

	unsigned int AndroidApplication::SdkVersion() const
	{
		unsigned int sdkVersion = 0;

		if (isInitialized_) {
			sdkVersion = AndroidJniHelper::SdkVersion();
		}

		return sdkVersion;
	}

	void AndroidApplication::enableAccelerometer(bool enabled)
	{
		if (isInitialized_) {
			AndroidInputManager::enableAccelerometer(enabled);
		}
	}

	const char* AndroidApplication::internalDataPath() const
	{
		return state_->activity->internalDataPath;
	}

	const char* AndroidApplication::externalDataPath() const
	{
		return state_->activity->externalDataPath;
	}

	const char* AndroidApplication::obbPath() const
	{
		return state_->activity->obbPath;
	}

	void AndroidApplication::toggleSoftInput()
	{
		if (isInitialized_) {
			AndroidJniWrap_InputMethodManager::toggleSoftInput();
		}
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	void AndroidApplication::preInit()
	{
		profileStartTime_ = TimeStamp::now();

#if defined(NCINE_LOG)
		// Try to open log file as early as possible
		StringView externalPath = externalDataPath();
		if (!externalPath.empty()) {
			__logFile = fs::Open(fs::JoinPath(externalPath, "Jazz2.log"_s), FileAccessMode::Write);
			if (!__logFile->IsOpened()) {
				__logFile = nullptr;
				LOGW("Cannot create log file, using Android log instead");
			}
		}
#endif

		AndroidJniHelper::AttachJVM(state_);
		AssetFile::InitializeAssetManager(state_);

		appEventHandler_ = createAppEventHandler_();
		// Only `onPreInit()` can modify the application configuration
		AppConfiguration& modifiableAppCfg = const_cast<AppConfiguration&>(appCfg_);
		appEventHandler_->onPreInit(modifiableAppCfg);
		LOGI("IAppEventHandler::onPreInit() invoked");
	}

	void AndroidApplication::init()
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

		timings_[Timings::PreInit] = profileStartTime_.secondsSince();

		Application::initCommon();

		isInitialized_ = true;
	}

	void AndroidApplication::shutdown()
	{
		AndroidJniHelper::DetachJVM();
		Application::shutdownCommon();
		isInitialized_ = false;
	}

	void AndroidApplication::setFocus(bool hasFocus)
	{
		Application::setFocus(hasFocus);

		// Check if a focus event has occurred
		if (hasFocus_ != hasFocus) {
			hasFocus_ = hasFocus;
			// Check if focus has been gained
			if (hasFocus) {
				theServiceLocator().audioDevice().unfreezePlayers();
			} else {
				theServiceLocator().audioDevice().freezePlayers();
			}
		}
	}
}
