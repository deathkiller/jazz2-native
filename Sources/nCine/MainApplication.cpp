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

#if defined(DEATH_TRACE) && defined(DEATH_TARGET_WINDOWS)

#	if !defined(ENABLE_VIRTUAL_TERMINAL_PROCESSING)
#		define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#	endif

#	include <Utf8.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

HANDLE __consoleHandleOut;
SHORT __consoleCursorY;
bool __showLogConsole;
bool __hasVirtualTerminal;
Array<wchar_t> __consolePrompt;

static bool EnableVirtualTerminalProcessing(HANDLE consoleHandleOut)
{
	if (consoleHandleOut == INVALID_HANDLE_VALUE) {
		return false;
	}

	DWORD dwMode = 0;
	return (::GetConsoleMode(consoleHandleOut, &dwMode) &&
			::SetConsoleMode(consoleHandleOut, dwMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING));
}

static bool CreateLogConsole(const StringView& title, bool& hasVirtualTerminal)
{
	FILE* fDummy = nullptr;

	if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
		HANDLE consoleHandleOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
		if (consoleHandleOut != INVALID_HANDLE_VALUE) {
			::freopen_s(&fDummy, "CONOUT$", "w", stdout);
			::setvbuf(stdout, NULL, _IONBF, 0);
		}

		HANDLE consoleHandleError = ::GetStdHandle(STD_ERROR_HANDLE);
		if (consoleHandleError != INVALID_HANDLE_VALUE) {
			::freopen_s(&fDummy, "CONOUT$", "w", stderr);
			::setvbuf(stderr, NULL, _IONBF, 0);
		}

		HANDLE consoleHandleIn = ::GetStdHandle(STD_INPUT_HANDLE);
		if (consoleHandleIn != INVALID_HANDLE_VALUE) {
			::freopen_s(&fDummy, "CONIN$", "r", stdin);
			::setvbuf(stdin, NULL, _IONBF, 0);
		}

		hasVirtualTerminal = EnableVirtualTerminalProcessing(consoleHandleOut);

		// Try to get command prompt to be able to reprint it when the game exits
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		if (::GetConsoleScreenBufferInfo(consoleHandleOut, &csbi)) {
			DWORD dwConsoleColumnWidth = (DWORD)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
			SHORT xEnd = csbi.dwCursorPosition.X;
			SHORT yEnd = csbi.dwCursorPosition.Y;
			if (xEnd != 0 || yEnd != 0) {
				DWORD dwNumberOfChars;
				SHORT yBegin = yEnd;
				if (dwConsoleColumnWidth > 16) {
					Array<wchar_t> tmp(NoInit, dwConsoleColumnWidth);
					while (yBegin > 0) {
						COORD dwReadCoord = { 0, yBegin };
						if (!::ReadConsoleOutputCharacter(consoleHandleOut, tmp.data(), dwConsoleColumnWidth, dwReadCoord, &dwNumberOfChars)) {
							break;
						}

						for (DWORD i = dwNumberOfChars - 8; i < dwNumberOfChars; i++) {
							wchar_t wchar = tmp[i];
							if (wchar != L' ') {
								yBegin--;
								continue;
							}
						}

						if (yBegin < yEnd) {
							yBegin++;
						}
						break;
					}
				}

				DWORD promptLength = (yEnd - yBegin) * dwConsoleColumnWidth + xEnd;
				__consolePrompt = Array<wchar_t>(NoInit, promptLength);
				COORD dwPromptCoord = { 0, yEnd };
				if (::ReadConsoleOutputCharacter(consoleHandleOut, __consolePrompt.data(), promptLength, dwPromptCoord, &dwNumberOfChars)) {
					if (::SetConsoleCursorPosition(consoleHandleOut, dwPromptCoord)) {
						::FillConsoleOutputCharacter(consoleHandleOut, L' ', promptLength, dwPromptCoord, &dwNumberOfChars);
					}
				} else {
					__consolePrompt = {};
				}
			}
		}

		return true;
	} else if (::AllocConsole()) {
		::freopen_s(&fDummy, "CONOUT$", "w", stdout);
		::freopen_s(&fDummy, "CONOUT$", "w", stderr);
		::freopen_s(&fDummy, "CONIN$", "r", stdin);

		HANDLE consoleHandleOut = ::CreateFile(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		HANDLE consoleHandleIn = ::CreateFile(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		::SetStdHandle(STD_OUTPUT_HANDLE, consoleHandleOut);
		::SetStdHandle(STD_ERROR_HANDLE, consoleHandleOut);
		::SetStdHandle(STD_INPUT_HANDLE, consoleHandleIn);

		hasVirtualTerminal = EnableVirtualTerminalProcessing(consoleHandleOut);

		::SetConsoleTitle(Death::Utf8::ToUtf16(title));
		HWND hWnd = ::GetConsoleWindow();
		if (hWnd != nullptr) {
			HINSTANCE inst = ((HINSTANCE)&__ImageBase);
			HICON windowIcon = (HICON)::LoadImage(inst, L"WINDOW_ICON", IMAGE_ICON, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_DEFAULTSIZE);
			HICON windowIconSmall = (HICON)::LoadImage(inst, L"WINDOW_ICON", IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTSIZE);
			if (windowIconSmall != NULL) ::SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)windowIconSmall);
			if (windowIcon != NULL) ::SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)windowIcon);
		}

		return true;
	} else {
		hasVirtualTerminal = false;
	}

	return false;
}

static void DestroyLogConsole()
{
	if (!__consolePrompt.empty()) {
		HANDLE consoleHandleOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
		if (consoleHandleOut != INVALID_HANDLE_VALUE) {
			CONSOLE_SCREEN_BUFFER_INFO csbi;
			if (::GetConsoleScreenBufferInfo(consoleHandleOut, &csbi)) {
				DWORD xEnd = csbi.dwCursorPosition.X;
				DWORD yEnd = csbi.dwCursorPosition.Y;
				if (xEnd != 0 || yEnd != 0) {
					DWORD dwNumberOfCharsWritten;
					::WriteConsoleW(consoleHandleOut, L"\r\n", (DWORD)arraySize(L"\r\n") - 1, &dwNumberOfCharsWritten, NULL);
					::WriteConsoleW(consoleHandleOut, __consolePrompt.data(), (DWORD)__consolePrompt.size(), &dwNumberOfCharsWritten, NULL);
				}
			}
		}
	}

	::FreeConsole();
}

#elif defined(DEATH_TRACE) && (defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_UNIX))

#include <unistd.h>

bool __hasVirtualTerminal;

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
		wchar_t pBuf[fs::MaxPathLength];
		DWORD pBufLength = ::GetModuleFileNameW(NULL, pBuf, (DWORD)arraySize(pBuf));
		if (pBufLength > 0) {
			wchar_t* lastSlash = wcsrchr(pBuf, L'\\');
			if (lastSlash == nullptr) {
				lastSlash = wcsrchr(pBuf, L'/');
			}
			if (lastSlash != nullptr) {
				lastSlash++;
				*lastSlash = '\0';
				::SetCurrentDirectoryW(pBuf);
			}
		}

		timeBeginPeriod(1);
#endif

#if defined(DEATH_TRACE)
#	if defined(DEATH_TARGET_APPLE)
		// Xcode's console reports that it is a TTY, but it doesn't support colors, TERM is not defined in this case
		__hasVirtualTerminal = isatty(1) && ::getenv("TERM");
#	elif defined(DEATH_TARGET_EMSCRIPTEN)
		char* userAgent = (char*)EM_ASM_PTR({
			return (typeof navigator !== 'undefined' && navigator !== null &&
					typeof navigator.userAgent !== 'undefined' && navigator.userAgent !== null
						? stringToNewUTF8(navigator.userAgent) : 0);
		});
		if (userAgent != nullptr) {
			// Only Chrome supports ANSI escape sequences for now
			__hasVirtualTerminal = (::strcasestr(userAgent, "chrome") != nullptr);
			free(userAgent);
		} else {
			__hasVirtualTerminal = false;
		}
#	elif defined(DEATH_TARGET_WINDOWS)
		// This can be initialized later from AttachTraceTarget()
		__showLogConsole = false;
		__hasVirtualTerminal = false;
#	elif defined(DEATH_TARGET_UNIX)
		::setvbuf(stdout, nullptr, _IONBF, 0);
		::setvbuf(stderr, nullptr, _IONBF, 0);
		__hasVirtualTerminal = isatty(1);
#	endif
#endif

		MainApplication& app = static_cast<MainApplication&>(theApplication());
		app.Init(createAppEventHandler, argc, argv);

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

#	if defined(DEATH_TRACE)
		if (__showLogConsole) {
			DestroyLogConsole();
		}
#	endif
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

	void MainApplication::AttachTraceTarget(Containers::StringView targetPath)
	{
#if defined(DEATH_TRACE) && defined(DEATH_TARGET_WINDOWS)
		if (targetPath == ConsoleTarget) {
			if (!__showLogConsole) {
				__showLogConsole = true;
				CreateLogConsole(NCINE_APP_NAME " [Console]", __hasVirtualTerminal);

				CONSOLE_SCREEN_BUFFER_INFO csbi;
				__consoleHandleOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
				if (::GetConsoleScreenBufferInfo(__consoleHandleOut, &csbi)) {
					__consoleCursorY = csbi.dwCursorPosition.Y;
				} else {
					__consoleHandleOut = NULL;
				}
			}
			return;
		}
#endif
		Application::AttachTraceTarget(targetPath);
	}
}
