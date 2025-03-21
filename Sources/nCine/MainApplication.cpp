﻿#include "MainApplication.h"
#include "IAppEventHandler.h"
#include "../Main.h"

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
#elif defined(DEATH_TARGET_UNIX)
#	include <pwd.h>
#	include <unistd.h>
#elif defined(DEATH_TARGET_WINDOWS)
#	include <timeapi.h>
#	include <Utf8.h>
#endif

#include "tracy.h"

using namespace Death;
using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace nCine::Backends;

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

	int MainApplication::Run(CreateAppEventHandlerDelegate createAppEventHandler, int argc, NativeArgument* argv)
	{
		if (createAppEventHandler == nullptr) {
			return EXIT_FAILURE;
		}

#if defined(DEATH_TARGET_SWITCH)
		socketInitializeDefault();
		nxlinkStdio();
		romfsInit();
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
			LOGI("Using working directory: %s", Utf8::FromUtf16(workingDir, workingDirLength).data());
		}
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
		emscripten_set_main_loop(MainApplication::EmscriptenStep, 0, 1);
		emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
#else
		while (!app.shouldQuit_) {
			app.ProcessStep();
		}
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

	void MainApplication::Quit()
	{
		Application::Quit();

#if defined(DEATH_TARGET_EMSCRIPTEN)
		// `window.close()` usually works only in PWA/standalone environment
		EM_ASM({
			window.close();
		});
#endif
	}

	bool MainApplication::EnablePlayStationExtendedSupport(bool enable)
	{
#if defined(WITH_SDL)
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, enable ? "1" : "0");
		// SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE inherits value from SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE
		//SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, enable ? "1" : "0");
		return true;
#else
		return false;
#endif
	}

	String MainApplication::GetUserName()
	{
#if defined(DEATH_TARGET_SWITCH)
		AccountUid uid;
		AccountProfile profile;
		if (R_SUCCEEDED(accountInitialize(AccountServiceType_Application))) {
			String userName;
			if (R_SUCCEEDED(accountGetPreselectedUser(&uid)) && R_SUCCEEDED(accountGetProfile(&profile, uid))) {
				AccountProfileBase profileBase;
				AccountUserData userData;
				accountProfileGet(&profile, &userData, &profileBase);
				String userName = profileBase.nickname;
				accountProfileClose(&profile);
			}
			accountExit();

			if (!userName.empty()) {
				return userName;
			}
		}
#elif defined(DEATH_TARGET_WINDOWS)
		wchar_t userName[64];
		DWORD userNameLength = (DWORD)arraySize(userName);
		if (::GetUserName(userName, &userNameLength) && userNameLength > 0) {
			return Utf8::FromUtf16(userName);
		}
#elif defined(DEATH_TARGET_APPLE)
		StringView userName = ::getenv("USER");
		if (!userName.empty()) {
			return userName;
		}
#elif defined(DEATH_TARGET_UNIX)
		struct passwd* pw = ::getpwuid(::getuid());
		if (pw != nullptr) {
			StringView userName = pw->pw_gecos;	// Display name
			if (!userName.empty()) {
				return userName;
			}
			userName = pw->pw_name;	// Plain name
			if (!userName.empty()) {
				return userName;
			}
		}

		StringView userName = ::getenv("USER");
		if (!userName.empty()) {
			return userName;
		}
#endif
		return {};
	}

	bool MainApplication::OpenUrl(StringView url)
	{
		if (!url.empty()) {
#if defined(DEATH_TARGET_EMSCRIPTEN)
			EM_ASM({
				var url = UTF8ToString($0, $1);
				if (url) window.open(url, '_blank');
			}, url.data(), url.size());
			return true;
#elif defined(WITH_SDL)
#	if SDL_VERSION_ATLEAST(2, 0, 14)
			return SDL_OpenURL(String::nullTerminatedView(url).data()) == 0;
#	endif
#endif
		}

		// TODO: Not implemented in GLFW
		return false;
	}

	void MainApplication::Init(CreateAppEventHandlerDelegate createAppEventHandler, int argc, NativeArgument* argv)
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

		if (appCfg_.withGraphics) {
			// Graphics device should always be created before the input manager!
			IGfxDevice::GLContextInfo glContextInfo(appCfg_);
			const DisplayMode::VSync vSyncMode = (appCfg_.withVSync ? DisplayMode::VSync::Enabled : DisplayMode::VSync::Disabled);
			DisplayMode displayMode(8, 8, 8, 8, 24, 8, DisplayMode::DoubleBuffering::Enabled, vSyncMode);

			const IGfxDevice::WindowMode windowMode(appCfg_.resolution.X, appCfg_.resolution.Y, appCfg_.windowPosition.X,
				appCfg_.windowPosition.Y, appCfg_.fullscreen, appCfg_.resizable, appCfg_.windowScaling);

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
				String windowIconFilePath = fs::CombinePath(GetDataPath(), appCfg_.windowIconFilename);
				if (fs::IsReadableFile(windowIconFilePath)) {
					gfxDevice_->setWindowIcon(windowIconFilePath);
				}
			}
		} else {
			gfxDevice_ = std::make_unique<NullGfxDevice>();
			inputManager_ = std::make_unique<NullInputManager>();
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
		if (appCfg_.withGraphics) {
			static_cast<Qt5InputManager&>(*inputManager_).updateJoystickStates();
		}
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
					if (appCfg_.withGraphics) {
						SdlInputManager::parseEvent(event);
					}
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

		if (appCfg_.withGraphics) {
			GlfwInputManager::updateJoystickStates();
		}
	}
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
	void MainApplication::EmscriptenStep()
	{
		static_cast<MainApplication&>(theApplication()).ProcessStep();
	}
#endif
}
