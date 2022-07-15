#include "PCApplication.h"
#include "IAppEventHandler.h"
#include "IO/FileSystem.h"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

#if defined(WITH_SDL)
#include "SdlGfxDevice.h"
#include "SdlInputManager.h"
#if WITH_NUKLEAR
#include "NuklearSdlInput.h"
#endif
#elif defined(WITH_GLFW)
#include "Graphics/GL/GlfwGfxDevice.h"
#include "Input/GlfwInputManager.h"
#elif defined(WITH_QT5)
#include "Qt5GfxDevice.h"
#include "Qt5InputManager.h"
#endif

#ifdef __EMSCRIPTEN__
#include "emscripten.h"
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

	int PCApplication::start(std::unique_ptr<IAppEventHandler>(*createAppEventHandler)(), int argc, char** argv)
	{
		if (createAppEventHandler == nullptr) {
			return EXIT_FAILURE;
		}

#if defined(_WIN32)
		// Set current directory, so everything is loaded correctly
		wchar_t pBuf[MAX_PATH];
		DWORD pBufLength = ::GetModuleFileName(NULL, pBuf, MAX_PATH);
		if (pBufLength > 0) {
			wchar_t* lastSlash = wcsrchr(pBuf, L'\\');
			if (lastSlash != nullptr) {
				lastSlash++;
				*lastSlash = '\0';
				::SetCurrentDirectory(pBuf);
			}
		}
#endif

		PCApplication& app = static_cast<PCApplication&>(theApplication());
		app.init(createAppEventHandler, argc, argv);

#ifndef __EMSCRIPTEN__
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

	void PCApplication::init(std::unique_ptr<IAppEventHandler>(*createAppEventHandler)(), int argc, char** argv)
	{
		profileStartTime_ = TimeStamp::now();
		wasSuspended_ = shouldSuspend();

		// Registering the logger as early as possible
		//theServiceLocator().registerLogger(std::make_unique<FileLogger>(appCfg_.consoleLogLevel));

		appEventHandler_ = createAppEventHandler();
		// Only `onPreInit()` can modify the application configuration
		AppConfiguration& modifiableAppCfg = const_cast<AppConfiguration&>(appCfg_);
		modifiableAppCfg.argc_ = argc;
		modifiableAppCfg.argv_ = argv;
		appEventHandler_->onPreInit(modifiableAppCfg);
		//LOGI("IAppEventHandler::onPreInit() invoked");

		// Setting log levels and filename based on application configuration
		//FileLogger &fileLogger = static_cast<FileLogger &>(theServiceLocator().logger());
		//fileLogger.setConsoleLevel(appCfg_.consoleLogLevel);
		//fileLogger.setFileLevel(appCfg_.fileLogLevel);
		//fileLogger.openLogFile(appCfg_.logFile.data());
		// Graphics device should always be created before the input manager!
		IGfxDevice::GLContextInfo glContextInfo(appCfg_);
		const DisplayMode::VSync vSyncMode = (appCfg_.withVSync ? DisplayMode::VSync::ENABLED : DisplayMode::VSync::DISABLED);
		DisplayMode displayMode(8, 8, 8, 8, 24, 8, DisplayMode::DoubleBuffering::ENABLED, vSyncMode);

		const IGfxDevice::WindowMode windowMode(appCfg_.resolution.X, appCfg_.resolution.Y, appCfg_.inFullscreen, appCfg_.isResizable);
#if defined(WITH_SDL)
		gfxDevice_ = std::make_unique<SdlGfxDevice>(windowMode, glContextInfo, displayMode);
		inputManager_ = std::make_unique<SdlInputManager>();
#elif defined(WITH_GLFW)
		gfxDevice_ = std::make_unique<GlfwGfxDevice>(windowMode, glContextInfo, displayMode);
		inputManager_ = std::make_unique<GlfwInputManager>();
#elif defined(WITH_QT5)
		//FATAL_ASSERT_MSG(qt5Widget_, "The Qt5 widget has not been assigned");
		gfxDevice_ = std::make_unique<Qt5GfxDevice>(windowMode, glContextInfo, displayMode, *qt5Widget_);
		inputManager_ = std::make_unique<Qt5InputManager>(*qt5Widget_);
#endif
		gfxDevice_->setWindowTitle(appCfg_.windowTitle.data());
		std::string windowIconFilePath = fs::joinPath(fs::dataPath(), appCfg_.windowIconFilename);
		if (fs::isReadableFile(windowIconFilePath.data())) {
			gfxDevice_->setWindowIcon(windowIconFilePath.data());
		}

		timings_[Timings::PRE_INIT] = profileStartTime_.secondsSince();

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
#if WITH_NUKLEAR
		NuklearSdlInput::inputBegin();
#endif

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_QUIT:
					shouldQuit_ = SdlInputManager::shouldQuitOnRequest();
					break;
				case SDL_WINDOWEVENT:
					if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
						setFocus(true);
					else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
						setFocus(false);
					else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
						gfxDevice_->width_ = event.window.data1;
						gfxDevice_->height_ = event.window.data2;
						resizeRootViewport(event.window.data1, event.window.data2);
					}
					break;
				default:
					SdlInputManager::parseEvent(event);
					break;
			}
#ifndef __EMSCRIPTEN__
			if (shouldSuspend()) {
				SDL_WaitEvent(&event);
				SDL_PushEvent(&event);
			}
#endif
		}

#if WITH_NUKLEAR
		NuklearSdlInput::inputEnd();
#endif
	}
#elif defined(WITH_GLFW)
	void PCApplication::processEvents()
	{
		// GLFW does not seem to correctly handle Emscripten focus and blur events
#ifndef __EMSCRIPTEN__
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

#ifdef __EMSCRIPTEN__
	void PCApplication::emscriptenStep()
	{
		reinterpret_cast<PCApplication&>(theApplication()).run();
	}
#endif

}
