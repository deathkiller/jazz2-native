#include "MainApplication.h"
#include "IAppEventHandler.h"
#include "Input/IInputManager.h"
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
#if defined(WITH_SDL) || defined(WITH_GLFW) || defined(WITH_QT5)
using namespace nCine::Backends;
#endif

#if defined(DEATH_TARGET_WINDOWS_RT)
#	error "For DEATH_TARGET_WINDOWS_RT, UwpApplication should be used instead of MainApplication"
#endif

#if defined(DEATH_TARGET_WINDOWS)
#	include <shellapi.h>
#	include <unknwn.h>

// {4ce576fa-83dc-4F88-951c-9d0782b4e376}
static const GUID CLSID_UIHostNoLaunch = { 0x4CE576FA, 0x83DC, 0x4f88, { 0x95, 0x1C, 0x9D, 0x07, 0x82, 0xB4, 0xE3, 0x76 } };
// {37c994e7_432b_4834_a2f7_dce1f13b834b}
static const GUID IID_ITipInvocation = { 0x37c994e7, 0x432b, 0x4834, { 0xa2, 0xf7, 0xdc, 0xe1, 0xf1, 0x3b, 0x83, 0x4b } };

struct ITipInvocation : IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE Toggle(HWND wnd) = 0;
};

static DWORD GetTabTipPathFromRegistry(wchar_t* dstPath, DWORD dstSize)
{
	HKEY hKey;
	if (::RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\Classes\\CLSID\\{054AAE20-4BEA-4347-8A35-64A533254A9D}\\LocalServer32"), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
		return 0;
	}

	DWORD type, cbData;
	if (::RegQueryValueEx(hKey, NULL, NULL, &type, NULL, &cbData) != ERROR_SUCCESS) {
		::RegCloseKey(hKey);
		return 0;
	}

	if ((type != REG_SZ && type != REG_EXPAND_SZ) || dstSize <= (cbData / sizeof(wchar_t))) {
		::RegCloseKey(hKey);
		return 0;
	}

	if (::RegQueryValueEx(hKey, NULL, NULL, NULL, reinterpret_cast<LPBYTE>(dstPath), &cbData) != ERROR_SUCCESS) {
		::RegCloseKey(hKey);
		return 0;
	}

	::RegCloseKey(hKey);
	return DWORD(wcsnlen(dstPath, dstSize));
}
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
	
	bool MainApplication::CanShowScreenKeyboard()
	{
#if defined(DEATH_TARGET_WINDOWS)
		return true;
#else
		return false;
#endif
	}

	bool MainApplication::ToggleScreenKeyboard()
	{
#if defined(DEATH_TARGET_WINDOWS)
		if (HideScreenKeyboard()) {
			return true;
		}

		return ShowScreenKeyboard();
#else
		return false;
#endif
	}

	bool MainApplication::ShowScreenKeyboard()
	{
#if defined(DEATH_TARGET_WINDOWS)
		if (::FindWindowEx(NULL, NULL, L"IPTip_Main_Window", NULL) != NULL) {
			// IID_ITipInvocation is supported only on Windows 10 and later
			ITipInvocation* tip;
			if (::CoCreateInstance(CLSID_UIHostNoLaunch, nullptr,
				CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER, IID_ITipInvocation, (void**)&tip) == S_OK) {
				HRESULT hr = tip->Toggle(::GetDesktopWindow());
				tip->Release();
				return SUCCEEDED(hr);
			}
		}

		// Create the process directly if the above fails on Windows 7
#	if defined(DEATH_TARGET_32BIT)
		PVOID redirectOldValue = nullptr;
		BOOL redirectSuccess = ::Wow64DisableWow64FsRedirection(&redirectOldValue);
#	endif
		bool success = false;
		wchar_t rawPath[MAX_PATH];
		if (GetTabTipPathFromRegistry(rawPath, DWORD(arraySize(rawPath))) == 0) {
			wcscpy_s(rawPath, L"\"%CommonProgramFiles%\\Microsoft Shared\\Ink\\TabTip.exe\"");
		}
		wchar_t path[MAX_PATH];
		DWORD pathLength = ::ExpandEnvironmentStringsW(rawPath, path, DWORD(arraySize(path)));
		if (pathLength > 0 && pathLength < DWORD(arraySize(path))) {
			HINSTANCE hinst = ::ShellExecuteW(NULL, L"open", path,
				nullptr, nullptr, SW_SHOWNORMAL);
			success = ((std::int32_t)hinst > 32);
		}
#	if defined(DEATH_TARGET_32BIT)
		if (redirectSuccess) {
			::Wow64RevertWow64FsRedirection(redirectOldValue);
		}
#	endif
		return success;
#else
		return false;
#endif
	}

	bool MainApplication::HideScreenKeyboard()
	{
#if defined(DEATH_TARGET_WINDOWS)
		HWND hwnd = ::FindWindowEx(NULL, NULL, L"IPTip_Main_Window", NULL);
		if (hwnd != NULL && ::IsWindowVisible(hwnd)) {
			// IID_ITipInvocation is supported only on Windows 10 and later
			ITipInvocation* tip;
			if (::CoCreateInstance(CLSID_UIHostNoLaunch, nullptr,
				CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER, IID_ITipInvocation, (void**)&tip) == S_OK) {
				HRESULT hr = tip->Toggle(::GetDesktopWindow());
				tip->Release();
				return SUCCEEDED(hr);
			}

			// Close the window if the above fails on Windows 7
			::PostMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
			return true;
		}
#endif

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
#if defined(WITH_GLFW) || defined(WITH_SDL)
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
