#include "PCApplication.h"
#include "IAppEventHandler.h"
#include "IO/FileSystem.h"
#include "../Common.h"

#if defined(WITH_SDL)
#	include "Graphics/GL/SdlGfxDevice.h"
#	include "Input/SdlInputManager.h"
#elif defined(WITH_GLFW)
#	include "Graphics/GL/GlfwGfxDevice.h"
#	include "Input/GlfwInputManager.h"
#elif defined(WITH_QT5)
#	include "Qt5GfxDevice.h"
#	include "Qt5InputManager.h"
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include "emscripten.h"
#endif

namespace nCine
{
	Application& theApplication()
	{
		static PCApplication instance;
		return instance;
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	int PCApplication::start(std::unique_ptr<IAppEventHandler>(*createAppEventHandler)(), int argc, NativeArgument* argv)
	{
		if (createAppEventHandler == nullptr) {
			return EXIT_FAILURE;
		}

#if defined(DEATH_TARGET_WINDOWS)
		// Force set current directory, so everything is loaded correctly, because it's not usually intended
		wchar_t pBuf[MAX_PATH];
		DWORD pBufLength = ::GetModuleFileName(nullptr, pBuf, _countof(pBuf));
		if (pBufLength > 0) {
			wchar_t* lastSlash = wcsrchr(pBuf, L'\\');
			if (lastSlash == nullptr) {
				lastSlash = wcsrchr(pBuf, L'/');
			}
			if (lastSlash != nullptr) {
				lastSlash++;
				*lastSlash = '\0';
				::SetCurrentDirectory(pBuf);
			}
		}
#endif

		PCApplication& app = static_cast<PCApplication&>(theApplication());
		app.init(createAppEventHandler, argc, argv);

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		while (!app.shouldQuit_) {
			app.run();
		}
#else
		emscripten_set_main_loop(PCApplication::emscriptenStep, 0, 1);
		emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
#endif
		app.shutdownCommon();

		return EXIT_SUCCESS;
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	void PCApplication::init(std::unique_ptr<IAppEventHandler>(*createAppEventHandler)(), int argc, NativeArgument* argv)
	{
		profileStartTime_ = TimeStamp::now();
		wasSuspended_ = shouldSuspend();
		appEventHandler_ = createAppEventHandler();

		// Only `onPreInit()` can modify the application configuration
		AppConfiguration& modifiableAppCfg = const_cast<AppConfiguration&>(appCfg_);
		modifiableAppCfg.argc_ = argc;
		modifiableAppCfg.argv_ = argv;
		appEventHandler_->onPreInit(modifiableAppCfg);
		LOGI("IAppEventHandler::onPreInit() invoked");

		// Graphics device should always be created before the input manager!
		IGfxDevice::GLContextInfo glContextInfo(appCfg_);
		const DisplayMode::VSync vSyncMode = (appCfg_.withVSync ? DisplayMode::VSync::Enabled : DisplayMode::VSync::Disabled);
		DisplayMode displayMode(8, 8, 8, 8, 24, 8, DisplayMode::DoubleBuffering::Enabled, vSyncMode);

		const IGfxDevice::WindowMode windowMode(appCfg_.resolution.X, appCfg_.resolution.Y, appCfg_.fullscreen, appCfg_.resizable, appCfg_.windowScaling);
#if defined(WITH_SDL)
		gfxDevice_ = std::make_unique<SdlGfxDevice>(windowMode, glContextInfo, displayMode);
		inputManager_ = std::make_unique<SdlInputManager>();
#elif defined(WITH_GLFW)
		gfxDevice_ = std::make_unique<GlfwGfxDevice>(windowMode, glContextInfo, displayMode);
		inputManager_ = std::make_unique<GlfwInputManager>();
#elif defined(WITH_QT5)
		FATAL_ASSERT_MSG(qt5Widget_, "The Qt5 widget has not been assigned");
		gfxDevice_ = std::make_unique<Qt5GfxDevice>(windowMode, glContextInfo, displayMode, *qt5Widget_);
		inputManager_ = std::make_unique<Qt5InputManager>(*qt5Widget_);
#endif
		gfxDevice_->setWindowTitle(appCfg_.windowTitle.data());
		if (!appCfg_.windowIconFilename.empty()) {
			String windowIconFilePath = fs::JoinPath(fs::GetDataPath(), appCfg_.windowIconFilename);
			if (fs::IsReadableFile(windowIconFilePath)) {
				gfxDevice_->setWindowIcon(windowIconFilePath);
			}
		}

		timings_[Timings::PreInit] = profileStartTime_.secondsSince();

#ifndef WITH_QT5
		// Common initialization on Qt5 is performed later, when OpenGL can be used
		initCommon();
#endif
	}

	void PCApplication::run()
	{
#if !defined(WITH_QT5)
		processEvents();
#elif defined(WITH_QT5GAMEPAD)
		static_cast<Qt5InputManager&>(*inputManager_).updateJoystickStates();
#endif

		const bool suspended = shouldSuspend();
		if (wasSuspended_ != suspended) {
			if (suspended) {
				suspend();
			} else {
				resume();
			}
			wasSuspended_ = suspended;
		}

		if (!suspended) {
			step();
		}
	}

#if defined(WITH_SDL)
	void PCApplication::processEvents()
	{
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_QUIT:
					shouldQuit_ = SdlInputManager::shouldQuitOnRequest();
					break;
				case SDL_DISPLAYEVENT:
					gfxDevice_->updateMonitors();
					break;
				case SDL_WINDOWEVENT:
					if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
						setFocus(true);
					else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
						setFocus(false);
					else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
						gfxDevice_->width_ = event.window.data1;
						gfxDevice_->height_ = event.window.data2;
						gfxDevice_->isFullscreen_ = SDL_GetWindowFlags(SDL_GetWindowFromID(event.window.windowID)) & SDL_WINDOW_FULLSCREEN;
						resizeScreenViewport(event.window.data1, event.window.data2);
					}
					break;
				default:
					SdlInputManager::parseEvent(event);
					break;
			}
#if !defined(DEATH_TARGET_EMSCRIPTEN)
			if (shouldSuspend()) {
				SDL_WaitEvent(&event);
				SDL_PushEvent(&event);
				// Don't lose any events when resuming
				while (SDL_PollEvent(&event)) {
					SDL_PushEvent(&event);
				}
			}
#endif
		}
	}
#elif defined(WITH_GLFW)
	void PCApplication::processEvents()
	{
		// GLFW does not seem to correctly handle Emscripten focus and blur events
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		setFocus(GlfwInputManager::hasFocus());
#endif

		if (shouldSuspend()) {
			glfwWaitEvents();
		} else {
			glfwPollEvents();
		}
		GlfwInputManager::updateJoystickStates();
	}
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
	void PCApplication::emscriptenStep()
	{
		reinterpret_cast<PCApplication&>(theApplication()).run();
	}
#endif

}
