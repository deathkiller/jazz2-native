#include "../../../Common.h"
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

/// Processes the next application command
void androidHandleCommand(struct android_app* state, int32_t cmd)
{
	nCine::AndroidApplication::ProcessCommand(state, cmd);
}

/// Parses the next input event
int32_t androidHandleInput(struct android_app* state, AInputEvent* event)
{
	return static_cast<int32_t>(nCine::AndroidInputManager::parseEvent(event));
}

namespace nCine
{
	Application& theApplication()
	{
		static AndroidApplication instance;
		return instance;
	}

	void AndroidApplication::Run(struct android_app* state, std::unique_ptr<IAppEventHandler>(*createAppEventHandler)())
	{
		ASSERT(state != nullptr);
		ASSERT(createAppEventHandler != nullptr);
		AndroidApplication& app = theAndroidApplication();
		app.state_ = state;
		app.createAppEventHandler_ = createAppEventHandler;

		state->onAppCmd = androidHandleCommand;
		state->onInputEvent = androidHandleInput;

		while (!app.ShouldQuit()) {
			int ident, events;
			struct android_poll_source* source;

			while ((ident = ALooper_pollOnce(app.ShouldSuspend() ? -1 : 0, nullptr, &events, reinterpret_cast<void**>(&source))) >= 0) {
				if (source != nullptr) {
					source->process(state, source);
				}
				if (ident == LOOPER_ID_USER) {
					AndroidInputManager::parseAccelerometerEvent();
				}
				if (state->destroyRequested) {
					LOGI("android_app->destroyRequested not equal to zero");
					app.Quit();
				}
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

	void AndroidApplication::ProcessCommand(struct android_app* state, int32_t cmd)
	{
		static EglGfxDevice* eglGfxDevice = nullptr;
		// A flag to avoid resuming if the application has not been suspended first
		static bool isSuspended = false;

		switch (cmd) {
			case APP_CMD_INPUT_CHANGED: {
				LOGW("APP_CMD_INPUT_CHANGED event received (not handled)");
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
				LOGW("APP_CMD_CONFIG_CHANGED event received (not handled)");
				break;
			}
			case APP_CMD_LOW_MEMORY: {
				LOGW("APP_CMD_LOW_MEMORY event received (not handled)");
				break;
			}
			case APP_CMD_START: {
				AndroidApplication& app = theAndroidApplication();
				if (!app.IsInitialized()) {
					app.PreInit();
					LOGI("APP_CMD_START event received (first run)");
				} else {
					LOGI("APP_CMD_START event received");
				}
				break;
			}
			case APP_CMD_RESUME: {
				LOGW("APP_CMD_RESUME event received");
				AndroidApplication& app = theAndroidApplication();
				app.isSuspended_ = false;
				if (isSuspended && !app.ShouldSuspend()) {
					isSuspended = false;
					app.Resume();
				}
				break;
			}
			case APP_CMD_SAVE_STATE: {
				LOGW("APP_CMD_SAVE_STATE event received (not handled)");
				break;
			}
			case APP_CMD_PAUSE: {
				LOGW("APP_CMD_PAUSE event received");
				AndroidApplication& app = theAndroidApplication();
				app.isSuspended_ = true;
				if (!isSuspended && app.ShouldSuspend()) {
					isSuspended = true;
					app.Suspend();
				}
				break;
			}
			case APP_CMD_STOP: {
				LOGW("APP_CMD_STOP event received (not handled)");
				break;
			}
			case APP_CMD_DESTROY: {
				LOGI("APP_CMD_DESTROY event received");
				theAndroidApplication().Quit();
				break;
			}
		}
	}

	const char* AndroidApplication::GetInternalDataPath() const
	{
		return state_->activity->internalDataPath;
	}

	const char* AndroidApplication::GetExternalDataPath() const
	{
		return state_->activity->externalDataPath;
	}

	const char* AndroidApplication::GetObbPath() const
	{
		return state_->activity->obbPath;
	}

	void AndroidApplication::ToggleSoftInput()
	{
		if (isInitialized_) {
			AndroidJniWrap_InputMethodManager::toggleSoftInput();
		}
	}

	void AndroidApplication::PreInit()
	{
		profileStartTime_ = TimeStamp::now();

		AndroidJniHelper::AttachJVM(state_);
		AndroidAssetStream::InitializeAssetManager(state_);
		
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
		timings_[(int)Timings::PreInit] = profileStartTime_.secondsSince();
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
