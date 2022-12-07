#include "PCApplication.h"
#include "IAppEventHandler.h"
#include "IO/FileSystem.h"
#include "../Common.h"

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
#endif

#include "tracy.h"

#if defined(NCINE_LOG) && defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)

#include <Utf8.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

bool showLogConsole_;

static bool CreateLogConsole(const StringView& title)
{
	FILE* fDummy = nullptr;

	if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
		HANDLE consoleHandleOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
		if (consoleHandleOut != INVALID_HANDLE_VALUE) {
			freopen_s(&fDummy, "CONOUT$", "w", stdout);
			setvbuf(stdout, NULL, _IONBF, 0);
		}

		HANDLE consoleHandleError = ::GetStdHandle(STD_ERROR_HANDLE);
		if (consoleHandleError != INVALID_HANDLE_VALUE) {
			freopen_s(&fDummy, "CONOUT$", "w", stderr);
			setvbuf(stderr, NULL, _IONBF, 0);
		}

		HANDLE consoleHandleIn = ::GetStdHandle(STD_INPUT_HANDLE);
		if (consoleHandleIn != INVALID_HANDLE_VALUE) {
			freopen_s(&fDummy, "CONIN$", "r", stdin);
			setvbuf(stdin, NULL, _IONBF, 0);
		}

		return true;
	} else if (::AllocConsole()) {
		freopen_s(&fDummy, "CONOUT$", "w", stdout);
		freopen_s(&fDummy, "CONOUT$", "w", stderr);
		freopen_s(&fDummy, "CONIN$", "r", stdin);

		HANDLE hConOut = ::CreateFile(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		HANDLE hConIn = ::CreateFile(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		::SetStdHandle(STD_OUTPUT_HANDLE, hConOut);
		::SetStdHandle(STD_ERROR_HANDLE, hConOut);
		::SetStdHandle(STD_INPUT_HANDLE, hConIn);

		::SetConsoleTitle(Death::Utf8::ToUtf16(title));
		HWND hWnd = ::GetConsoleWindow();
		if (hWnd != nullptr) {
			HINSTANCE inst = ((HINSTANCE)&__ImageBase);
			HICON hIcon = (HICON)::LoadImage(inst, L"LOG_ICON", IMAGE_ICON, 32, 32, LR_DEFAULTSIZE);
			HICON hIconSmall = (HICON)::LoadImage(inst, L"LOG_ICON", IMAGE_ICON, 16, 16, LR_DEFAULTSIZE);
			::SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
			::SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
		}

		return true;
	}

	return false;
}

static void DestroyLogConsole()
{
	// The "Enter" key is only sent if the console window is in focus
	if (::GetConsoleWindow() == ::GetForegroundWindow()) {
		// Send the "Enter" key to the console to release the command prompt
		INPUT ip;
		ip.type = INPUT_KEYBOARD;
		ip.ki.wScan = 0;
		ip.ki.time = 0;
		ip.ki.dwExtraInfo = 0;

		ip.ki.wVk = 0x0D; // virtual-key code for the "Enter" key
		ip.ki.dwFlags = 0; // 0 for key press
		::SendInput(1, &ip, sizeof(INPUT));

		ip.ki.dwFlags = KEYEVENTF_KEYUP; // `KEYEVENTF_KEYUP` for key release
		::SendInput(1, &ip, sizeof(INPUT));
	}

	::FreeConsole();
}

static bool EnableVirtualTerminalProcessing()
{
	HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE) {
		return false;
	}
	DWORD dwMode = 0;
	if (!::GetConsoleMode(hOut, &dwMode)) {
		return false;
	}
	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	if (!::SetConsoleMode(hOut, dwMode)) {
		return false;
	}
	return true;
}

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

#if defined(NCINE_LOG) && defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		if (showLogConsole_) {
			DestroyLogConsole();
		}
#endif

		return EXIT_SUCCESS;
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	void PCApplication::init(std::unique_ptr<IAppEventHandler>(*createAppEventHandler)(), int argc, NativeArgument* argv)
	{
		ZoneScoped;
		profileStartTime_ = TimeStamp::now();
		wasSuspended_ = shouldSuspend();

#if defined(NCINE_LOG)
#	if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		showLogConsole_ = false;
		for (int i = 0; i < argc; i++) {
			if (wcscmp(argv[i], L"/log") == 0) {
				showLogConsole_ = true;
				break;
			}
		}
		if (showLogConsole_) {
			CreateLogConsole(NCINE_APP_NAME ": Console");
			EnableVirtualTerminalProcessing();
		}
#	elif defined(DEATH_TARGET_UNIX)
		setvbuf(stdout, nullptr, _IONBF, 0);
		setvbuf(stderr, nullptr, _IONBF, 0);
#	endif
#endif

		appEventHandler_ = createAppEventHandler();

		// Only `onPreInit()` can modify the application configuration
		appCfg_.argc_ = argc;
		appCfg_.argv_ = argv;
		appEventHandler_->onPreInit(appCfg_);
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
					switch (event.window.event) {
						case SDL_WINDOWEVENT_FOCUS_GAINED:
							setFocus(true);
							break;
						case SDL_WINDOWEVENT_FOCUS_LOST:
							setFocus(false);
							break;
						case SDL_WINDOWEVENT_SIZE_CHANGED:
							gfxDevice_->width_ = event.window.data1;
							gfxDevice_->height_ = event.window.data2;
							SDL_Window* windowHandle = SDL_GetWindowFromID(event.window.windowID);
							gfxDevice_->isFullscreen_ = (SDL_GetWindowFlags(windowHandle) & SDL_WINDOW_FULLSCREEN) != 0;
							SDL_GL_GetDrawableSize(windowHandle, &gfxDevice_->drawableWidth_, &gfxDevice_->drawableHeight_);
							resizeScreenViewport(gfxDevice_->drawableWidth_, gfxDevice_->drawableHeight_);
							break;
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
