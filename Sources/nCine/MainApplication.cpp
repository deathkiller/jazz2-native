#include "MainApplication.h"
#include "IAppEventHandler.h"
#include "../Common.h"

#include <IO/FileSystem.h>

#if defined(WITH_SDL)
#	include "Backends/SdlGfxDevice.h"
#	include "Backends/SdlInputManager.h"
#elif defined(WITH_GLFW)
#	include "Backends/GlfwGfxDevice.h"
#	include "Backends/GlfwInputManager.h"
#elif defined(WITH_QT5)
#	include "Backends/Qt5GfxDevice.h"
#	include "Backends/Qt5InputManager.h"
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/emscripten.h>
#elif defined(DEATH_TARGET_SWITCH)
#	include <switch.h>
#elif defined(DEATH_TARGET_WINDOWS)
#	include <timeapi.h>
#	include <Utf8.h>
#endif

#include "tracy.h"

using namespace Death;
using namespace Death::Containers::Literals;
using namespace Death::IO;

#if defined(DEATH_TARGET_WINDOWS_RT)
#	error "For DEATH_TARGET_WINDOWS_RT, UwpApplication should be used instead of MainApplication"
#endif

namespace nCine
{
	Application& theApplication()
	{
		static MainApplication instance;
		return instance;
	}

	int MainApplication::Run(std::unique_ptr<IAppEventHandler>(*createAppEventHandler)(), int argc, NativeArgument* argv)
	{
		if (createAppEventHandler == nullptr) {
			return EXIT_FAILURE;
		}

#if defined(DEATH_TARGET_SWITCH)
		socketInitializeDefault();
		nxlinkStdio();
		romfsInit();

		// Initialize the default gamepad
		padConfigureInput(1, HidNpadStyleSet_NpadStandard);
		PadState pad;
		padInitializeDefault(&pad);
		hidInitializeTouchScreen();
#elif defined(DEATH_TARGET_WINDOWS)
		// Force set current directory, so everything is loaded correctly, because it's not usually intended
		wchar_t workingDir[fs::MaxPathLength];
		DWORD workingDirLength = ::GetModuleFileNameW(NULL, workingDir, (DWORD)arraySize(workingDir));
		if (workingDirLength > 0) {
			wchar_t* lastSlash = wcsrchr(workingDir, L'\\');
			if (lastSlash == nullptr) {
				lastSlash = wcsrchr(workingDir, L'/');
			}
			if (lastSlash != nullptr) {
				lastSlash++;
				workingDirLength = (DWORD)(lastSlash - workingDir);
				*lastSlash = '\0';
				if (!::SetCurrentDirectoryW(workingDir)) {
					LOGE("Failed to change working directory with error 0x%08x", ::GetLastError());
					workingDirLength = 0;
				}
			} else {
				workingDirLength = 0;
			}
		}

		timeBeginPeriod(1);
#endif

		MainApplication& app = static_cast<MainApplication&>(theApplication());
		app.Init(createAppEventHandler, argc, argv);

#if defined(DEATH_TARGET_WINDOWS)
		if (workingDirLength > 0) {
			LOGI("Current working directory: %s", Utf8::FromUtf16(workingDir, workingDirLength).data());
		}
#endif

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		while (!app.shouldQuit_) {
			app.ProcessStep();
		}
#else
		emscripten_set_main_loop(MainApplication::EmscriptenStep, 0, 1);
		emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
#endif

		app.ShutdownCommon();

#if defined(DEATH_TARGET_SWITCH)
		romfsExit();
		socketExit();
#elif defined(DEATH_TARGET_WINDOWS)
		timeEndPeriod(1);
#endif
		return EXIT_SUCCESS;
	}

	void MainApplication::Init(std::unique_ptr<IAppEventHandler>(*createAppEventHandler)(), int argc, NativeArgument* argv)
	{
		ZoneScopedC(0x81A861);
#if defined(NCINE_PROFILING)
		profileStartTime_ = TimeStamp::now();
#endif
		wasSuspended_ = ShouldSuspend();

		// Only `OnPreInit()` can modify the application configuration
		if (argc > 1) {
#if defined(DEATH_TARGET_WINDOWS)
			appCfg_.argv_ = Array<String>(argc - 1);
			for (std::int32_t i = 1; i < argc; i++) {
				appCfg_.argv_[i - 1] = Utf8::FromUtf16(argv[i]);
			}
#else
			appCfg_.argv_ = Array<StringView>(argc - 1);
			for (std::int32_t i = 1; i < argc; i++) {
				appCfg_.argv_[i - 1] = argv[i];
			}
#endif
		}

		PreInitCommon(createAppEventHandler());

		if (shouldQuit_) {
			// If the app was quit from OnPreInit(), skip further initialization
			return;
		}

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
			String windowIconFilePath = fs::CombinePath(theApplication().GetDataPath(), appCfg_.windowIconFilename);
			if (fs::IsReadableFile(windowIconFilePath)) {
				gfxDevice_->setWindowIcon(windowIconFilePath);
			}
		}

#if defined(NCINE_PROFILING)
		timings_[(std::int32_t)Timings::PreInit] = profileStartTime_.secondsSince();
#endif
#if !defined(WITH_QT5)
		// Common initialization on Qt5 is performed later, when OpenGL can be used
		InitCommon();
#endif
	}

	void MainApplication::ProcessStep()
	{
#if !defined(WITH_QT5)
		ProcessEvents();
#elif defined(WITH_QT5GAMEPAD)
		static_cast<Qt5InputManager&>(*inputManager_).updateJoystickStates();
#endif

		const bool suspended = ShouldSuspend();
		if (wasSuspended_ != suspended) {
			if (suspended) {
				Suspend();
			} else {
				Resume();
			}
			wasSuspended_ = suspended;
		}

		if (!suspended) {
			Step();
		}
	}

#if defined(WITH_SDL)
	void MainApplication::ProcessEvents()
	{
		ZoneScoped;

		SDL_Event event;
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (ShouldSuspend()) {
			SDL_WaitEvent(&event);
			SDL_PushEvent(&event);
			// Don't lose any events when resuming
			while (SDL_PollEvent(&event)) {
				SDL_PushEvent(&event);
			}
		}
#	endif
		
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_QUIT:
					if (SdlInputManager::shouldQuitOnRequest()) {
						shouldQuit_ = true;
					}
					break;
				case SDL_DISPLAYEVENT:
					gfxDevice_->updateMonitors();
					break;
				case SDL_WINDOWEVENT:
					switch (event.window.event) {
						case SDL_WINDOWEVENT_FOCUS_GAINED:
							SetFocus(true);
							break;
						case SDL_WINDOWEVENT_FOCUS_LOST:
							SetFocus(false);
							break;
						case SDL_WINDOWEVENT_SIZE_CHANGED:
							gfxDevice_->width_ = event.window.data1;
							gfxDevice_->height_ = event.window.data2;
							SDL_Window* windowHandle = SDL_GetWindowFromID(event.window.windowID);
							gfxDevice_->isFullscreen_ = (SDL_GetWindowFlags(windowHandle) & SDL_WINDOW_FULLSCREEN) != 0;
							SDL_GL_GetDrawableSize(windowHandle, &gfxDevice_->drawableWidth_, &gfxDevice_->drawableHeight_);
							ResizeScreenViewport(gfxDevice_->drawableWidth_, gfxDevice_->drawableHeight_);
							break;
					}
					break;
				default:
					SdlInputManager::parseEvent(event);
					break;
			}
		}
	}
#elif defined(WITH_GLFW)
	void MainApplication::ProcessEvents()
	{
		// GLFW does not seem to correctly handle Emscripten focus and blur events
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		SetFocus(GlfwInputManager::hasFocus());
#	endif

		if (ShouldSuspend()) {
			glfwWaitEvents();
		} else {
			glfwPollEvents();
		}
		GlfwInputManager::updateJoystickStates();
	}
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
	void MainApplication::EmscriptenStep()
	{
		static_cast<MainApplication&>(theApplication()).ProcessStep();
	}
#endif
}
