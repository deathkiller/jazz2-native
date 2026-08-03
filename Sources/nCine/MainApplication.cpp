#include "MainApplication.h"
#include "IAppEventHandler.h"
#include "Input/IInputManager.h"
#include "../Main.h"

#include <IO/FileSystem.h>

#if (defined(WITH_SDL2) || defined(WITH_SDL3))
#	include "Backends/SdlGfxDevice.h"
#	include "Backends/SdlInputManager.h"
#elif defined(WITH_GLFW)
#	include "Backends/GlfwGfxDevice.h"
#	include "Backends/GlfwInputManager.h"
#elif defined(WITH_QT5)
#	include "Backends/Qt5GfxDevice.h"
#	include "Backends/Qt5InputManager.h"
#elif defined(WITH_OGC)
#	include "Backends/Ogc/OgcGfxDevice.h"
#	include "Backends/Ogc/OgcInputManager.h"
#elif defined(WITH_DC)
#	include "Backends/Dc/DcGfxDevice.h"
#	include "Backends/Dc/DcInputManager.h"
#elif defined(WITH_PSP)
#	include "Backends/Psp/PspGfxDevice.h"
#	include "Backends/Psp/PspInputManager.h"
#endif

// For resizing the swap chain when the window size changes (the call is uniform across the backends;
// it is a no-op on the OpenGL and software backends, which have no backend-owned swap chain)
#include "Graphics/RHI/Rhi.h"

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/emscripten.h>
#elif defined(DEATH_TARGET_SWITCH)
#	include <switch.h>
#elif defined(DEATH_TARGET_VITA)
#	include <vitasdk.h>
#	include <vitaGL.h>
#elif defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
#	include <gccore.h>
#	include <fat.h>
#	include <ogc/pad.h>
#	include <sys/iosupport.h>
#	if defined(DEATH_TARGET_WII)
#		include <wiiuse/wpad.h>
#	endif
#elif defined(DEATH_TARGET_DREAMCAST)
#	include <kos.h>
#elif defined(DEATH_TARGET_PSP)
#	include <pspkernel.h>
#	include <pspdebug.h>
#	include <psppower.h>
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
#if (defined(WITH_SDL2) || defined(WITH_SDL3)) || defined(WITH_GLFW) || defined(WITH_QT5) || defined(WITH_OGC) || defined(WITH_DC) || defined(WITH_PSP)
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

#if defined(DEATH_TARGET_PSP)
// The firmware reads these out of the ELF to decide how to load it, so they have to exist at namespace
// scope in the executable itself, and the module name has to stay plain ASCII (it goes into a 27-byte
// field), which is why it is NCINE_APP rather than NCINE_APP_NAME.
//
// A negative heap size means "everything that is free, less the threshold the firmware keeps for itself"
// (PSPSDK's _sbrk reads the value as kilobytes and treats any negative one that way), which is what a
// game that loads its assets into main memory wants - a fixed budget would only leave memory unusable.
//
// PSP_MAIN_THREAD_ATTR asks for the VFPU, whose vector unit is the Allegrex's only fast float path. The
// shared math code compiles to ordinary FPU instructions today, but a thread that was not created with
// THREAD_ATTR_VFPU cannot use it at all, so the attribute is set now rather than being a surprise later.
PSP_MODULE_INFO(NCINE_APP, PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1);
#endif

namespace nCine
{
#if defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
	// Set from the power/reset event callbacks, which run in interrupt context and must not do more
	// than this - the main loop picks it up and shuts the game down in an orderly way
	static volatile bool ogcShutdownRequested = false;

	static ssize_t OgcNullWrite(struct _reent* r, void* fd, const char* ptr, size_t len)
	{
		return len;
	}

	// Replaces the boot console's stdout once the real video device owns the screen (see below)
	static const devoptab_t ogcNullOut = { "stdout", 0, nullptr, nullptr, OgcNullWrite };
#endif

#if defined(DEATH_TARGET_PSP)
	// Set from the HOME-button exit callback, which runs on its own firmware thread and must not do more
	// than this - the main loop picks it up and shuts the game down in an orderly way (exactly like the
	// libogc power/reset callbacks above)
	static volatile bool pspShutdownRequested = false;

	static int PspExitCallback(int arg1, int arg2, void* common)
	{
		static_cast<void>(arg1); static_cast<void>(arg2); static_cast<void>(common);
		pspShutdownRequested = true;
		return 0;
	}

	// The callback can only be registered from a thread that then sleeps waiting for callbacks, so it needs
	// a thread of its own - this is the standard PSPSDK arrangement (sceKernelSleepThreadCB is what makes
	// the firmware deliver them). Without it the HOME button does nothing and the console can only be
	// switched off, which is why every PSP title sets this up before anything else.
	static int PspCallbackThread(SceSize args, void* argp)
	{
		static_cast<void>(args); static_cast<void>(argp);
		int callbackId = sceKernelCreateCallback("JazzExitCallback", PspExitCallback, nullptr);
		sceKernelRegisterExitCallback(callbackId);
		sceKernelSleepThreadCB();
		return 0;
	}
#endif

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
#elif defined(DEATH_TARGET_VITA)
		// Enable analog sampling for controllers
		sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
		sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

		// Enabling sampling for the analogs
		sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
#elif defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
		// Bring up the shared subsystems before any device exists: the video hardware (OgcGfxDevice picks
		// the mode and allocates framebuffers later), the SD/storage FAT layer and the controller ports
		VIDEO_Init();

		// Early boot console: everything written to stdout (including all trace messages) is shown
		// directly on the screen until OgcGfxDevice reconfigures the video mode during initialization,
		// so startup crashes are visible without a USB Gecko attached
		GXRModeObj* earlyMode = VIDEO_GetPreferredMode(NULL);
		void* earlyXfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(earlyMode));
		CON_Init(earlyXfb, 20, 20, earlyMode->fbWidth, earlyMode->xfbHeight, earlyMode->fbWidth * VI_DISPLAY_PIX_SZ);
		VIDEO_Configure(earlyMode);
		VIDEO_SetNextFramebuffer(earlyXfb);
		VIDEO_SetBlack(FALSE);
		VIDEO_Flush();
		VIDEO_WaitVSync();
		printf("Application starting...\n");

		if (!fatInitDefault()) {
			// Without a FAT device there is no game content and no writable storage, so halt with
			// a readable message instead of crashing on the missing device later
#	if defined(DEATH_TARGET_GAMECUBE)
			printf("\n  Cannot access the SD card!\n\n  Insert an SD card with the game files into the SD Gecko adapter.\n");
#	else
			printf("\n  Cannot access the SD card!\n\n  Insert an SD card with the game files and restart the console.\n");
#	endif
			while (true) {
				VIDEO_WaitVSync();
			}
		}
		PAD_Init();
#	if defined(DEATH_TARGET_WII)
		WPAD_Init();
#	endif

		// The console (and an emulator asking the title to stop) requests shutdown through these events;
		// a title that ignores them keeps running until it is killed, so each one asks the game to quit
		SYS_SetResetCallback([](std::uint32_t irq, void* ctx) {
			static_cast<void>(irq); static_cast<void>(ctx);
			ogcShutdownRequested = true;
		});
#	if defined(DEATH_TARGET_WII)
		// The GameCube has no power event - its power switch cuts the supply directly
		SYS_SetPowerCallback([]() { ogcShutdownRequested = true; });
		WPAD_SetPowerButtonCallback([](std::int32_t chan) {
			static_cast<void>(chan);
			ogcShutdownRequested = true;
		});
#	endif
#elif defined(DEATH_TARGET_DREAMCAST)
		// Early boot log on the framebuffer console: startup messages (including all trace messages)
		// are shown directly on the screen, so crashes are visible without a serial cable attached;
		// DcGfxDevice switches dbgio back to the serial port when the real renderer takes over
		dbgio_dev_select("fb");
		printf("Application starting...\n");
#elif defined(DEATH_TARGET_PSP)
		// The HOME button has to be answered by the application itself, so its callback goes up first -
		// before anything can go wrong during initialization and leave the console with no way out
		{
			int threadId = sceKernelCreateThread("CallbackThread", PspCallbackThread, 0x11, 0xFA0, 0, nullptr);
			if (threadId >= 0) {
				sceKernelStartThread(threadId, 0, nullptr);
			}
		}

		// The console boots at 222 MHz to save battery; every game raises it to the maximum the hardware
		// allows. The bus clock has to be exactly half the CPU clock, which is why 333/166 rather than
		// 333/333 - the firmware rejects any other ratio.
		scePowerSetClockFrequency(333, 333, 166);

		// Early boot log on the debug screen: startup messages (including all trace messages) are shown
		// directly on the screen, so crashes are visible before the renderer exists. PspGfxDevice's GU
		// session takes the framebuffer over during Init(), after which the trace sink keeps writing to
		// stdout alone - which the firmware discards, but PPSSPP shows in its log.
		pspDebugScreenInit();
		printf("Application starting...\n");
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
					LOGE("Failed to change working directory with error 0x{:.8x}", ::GetLastError());
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

#if defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
		// The boot console is invisible from the moment OgcGfxDevice installed its own framebuffers
		// during Init() (its constructor waits for the flip), but the console driver would keep
		// rendering every log line into the old buffer. So stdout is routed into the void first,
		// and only then can the boot framebuffer's ~600 KB go back to MEM1.
		devoptab_list[STD_OUT] = &ogcNullOut;
		free(MEM_K1_TO_K0(earlyXfb));
#endif

#if defined(DEATH_TARGET_WINDOWS)
		if (workingDirLength > 0) {
			LOGI("Using working directory: \"{}\"", Utf8::FromUtf16(workingDir, workingDirLength));
		}
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
		emscripten_set_main_loop(MainApplication::EmscriptenStep, 0, 1);
		emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
#else
		while (!app.shouldQuit_) {
			app.ProcessStep();
#	if defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
			if (ogcShutdownRequested) {
				app.Quit();
			}
#	elif defined(DEATH_TARGET_PSP)
			if (pspShutdownRequested) {
				app.Quit();
			}
#	endif
		}
#endif

		app.ShutdownCommon();

#if defined(DEATH_TARGET_SWITCH)
		romfsExit();
		socketExit();
#elif defined(DEATH_TARGET_VITA)
		sceKernelExitProcess(0);
#elif defined(DEATH_TARGET_PSP)
		// Returning from main() would land back in the crt0 stub, which halts; the firmware expects a
		// finished application to hand control back to whatever launched it instead
		sceKernelExitGame();
#elif defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
		// Returning from main() only reaches a loader when one left its return stub behind (the Homebrew
		// Channel and friends); booted directly there is nothing to return to, so the console is asked to
		// end the title instead of running off into whatever follows the entry point
		fatUnmount("sd:");
		fatUnmount("carda:");
#	if defined(DEATH_TARGET_WII)
		if (*(volatile std::uint32_t*)0x80001804 != 0x53545542 /*"STUB"*/) {
			SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
		}
#	else
		SYS_ResetSystem(SYS_HOTRESET, 0, 0);
#	endif
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
#if defined(WITH_SDL3)
		// SDL3 merged the per-controller HIDAPI rumble hints into SDL_HINT_JOYSTICK_ENHANCED_REPORTS
		SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, enable ? "1" : "0");
		return true;
#elif defined(WITH_SDL2)
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
#elif (defined(WITH_SDL2) || defined(WITH_SDL3))
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
			success = ((INT_PTR)hinst > 32);
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
			appCfg_.argv_ = Array<String>(ValueInit, argc - 1);
			for (std::int32_t i = 1; i < argc; i++) {
				appCfg_.argv_[i - 1] = Utf8::FromUtf16(argv[i]);
			}
#elif defined(DEATH_TARGET_APPLE)
			// Cocoa supports -Key value options which set the user defaults key "Key" to the value "value",
			// skip them here, because they are handled internally by the operating system
			std::int32_t count = 0;
			std::int32_t i = 1;
			while (i < argc) {
				const char* arg = argv[i];
				if (arg[0] == '-' && arg[1] == 'N' && arg[2] == 'S') {
					i += 2; // Skip key and value
				} else {
					count++;
					i++;
				}
			}

			if (count > 0) {
				appCfg_.argv_ = Array<StringView>(ValueInit, count);
				count = 0;
				i = 1;
				while (i < argc) {
					const char* arg = argv[i];
					if (arg[0] == '-' && arg[1] == 'N' && arg[2] == 'S') {
						i += 2; // Skip key and value
					} else {
						appCfg_.argv_[count++] = arg;
						i++;
					}
				}
			}
#else
			appCfg_.argv_ = Array<StringView>(ValueInit, argc - 1);
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

#if defined(DEATH_DEBUG)
#	define INIT_MESSAGE_SUFFIX " in debug configuration"
#else
#	define INIT_MESSAGE_SUFFIX ""
#endif

		if (appCfg_.withGraphics) {
#if defined(WITH_GLFW)
			LOGI(NCINE_APP_NAME " v" NCINE_VERSION " (GLFW) initializing" INIT_MESSAGE_SUFFIX "...");
#elif defined(WITH_SDL3)
			LOGI(NCINE_APP_NAME " v" NCINE_VERSION " (SDL3) initializing" INIT_MESSAGE_SUFFIX "...");
#elif defined(WITH_SDL2)
			LOGI(NCINE_APP_NAME " v" NCINE_VERSION " (SDL2) initializing" INIT_MESSAGE_SUFFIX "...");
#else
			LOGI(NCINE_APP_NAME " v" NCINE_VERSION " initializing" INIT_MESSAGE_SUFFIX "...");
#endif

			LOGB("Initializing graphics device and input manager...");

			// Graphics device should always be created before the input manager
			IGfxDevice::ContextInfo contextInfo(appCfg_);
			const DisplayMode::VSync vSyncMode = (appCfg_.withVSync ? DisplayMode::VSync::Enabled : DisplayMode::VSync::Disabled);
			DisplayMode displayMode(8, 8, 8, 8, 24, 8, DisplayMode::DoubleBuffering::Enabled, vSyncMode);

			const IGfxDevice::WindowMode windowMode(appCfg_.resolution.X, appCfg_.resolution.Y, appCfg_.windowPosition.X,
				appCfg_.windowPosition.Y, appCfg_.fullscreen, appCfg_.resizable, appCfg_.windowScaling);

#if (defined(WITH_SDL2) || defined(WITH_SDL3))
			gfxDevice_ = std::make_unique<SdlGfxDevice>(windowMode, contextInfo, displayMode);
			inputManager_ = std::make_unique<SdlInputManager>();
#elif defined(WITH_GLFW)
			gfxDevice_ = std::make_unique<GlfwGfxDevice>(windowMode, contextInfo, displayMode);
			inputManager_ = std::make_unique<GlfwInputManager>();
#elif defined(WITH_QT5)
			FATAL_ASSERT_MSG(qt5Widget_, "The Qt5 widget has not been assigned");
			gfxDevice_ = std::make_unique<Qt5GfxDevice>(windowMode, contextInfo, displayMode, *qt5Widget_);
			inputManager_ = std::make_unique<Qt5InputManager>(*qt5Widget_);
#elif defined(WITH_OGC)
			gfxDevice_ = std::make_unique<OgcGfxDevice>(windowMode, contextInfo, displayMode);
			inputManager_ = std::make_unique<OgcInputManager>();
#elif defined(WITH_DC)
			gfxDevice_ = std::make_unique<DcGfxDevice>(windowMode, contextInfo, displayMode);
			inputManager_ = std::make_unique<DcInputManager>();
#elif defined(WITH_PSP)
			gfxDevice_ = std::make_unique<PspGfxDevice>(windowMode, contextInfo, displayMode);
			inputManager_ = std::make_unique<PspInputManager>();
#endif
			gfxDevice_->setWindowTitle(appCfg_.windowTitle.data());
			if (!appCfg_.windowIconFilename.empty()) {
				String windowIconFilePath = fs::CombinePath(GetDataPath(), appCfg_.windowIconFilename);
				if (fs::IsReadableFile(windowIconFilePath)) {
					gfxDevice_->setWindowIcon(windowIconFilePath);
				}
			}
		} else {
			LOGI(NCINE_APP_NAME " v" NCINE_VERSION " initializing" INIT_MESSAGE_SUFFIX "...");

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
		if (appCfg_.withGraphics) {
#if defined(WITH_GLFW) || (defined(WITH_SDL2) || defined(WITH_SDL3))
			ProcessEvents();
#elif defined(WITH_QT5GAMEPAD)
			static_cast<Qt5InputManager&>(*inputManager_).updateJoystickStates();
#elif defined(WITH_OGC)
			// No window events on a console; polling the controller ports is the whole event pump
			OgcInputManager::updateJoystickStates();
#elif defined(WITH_DC)
			// No window events on a console; polling the maple bus is the whole event pump
			DcInputManager::updateJoystickStates();
#elif defined(WITH_PSP)
			// No window events on a console; polling the controller service is the whole event pump
			PspInputManager::updateJoystickStates();
#endif
		}

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

#if defined(WITH_SDL3)
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
			// SDL3 flattened the SDL2 SDL_WINDOWEVENT/SDL_DISPLAYEVENT umbrellas into one event type per action,
			// so the sub-events are dispatched directly off event.type here instead of event.window.event
			switch (event.type) {
				case SDL_EVENT_QUIT:
					if (SdlInputManager::shouldQuitOnRequest()) {
						shouldQuit_ = true;
					}
					break;
				case SDL_EVENT_WINDOW_FOCUS_GAINED:
					if (SdlGfxDevice::isMainWindow(event.window.windowID)) {
						SetFocus(true);
					}
					break;
				case SDL_EVENT_WINDOW_FOCUS_LOST:
					if (SdlGfxDevice::isMainWindow(event.window.windowID)) {
						SetFocus(false);
					}
					break;
				case SDL_EVENT_WINDOW_RESIZED:
				case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
					if (SdlGfxDevice::isMainWindow(event.window.windowID)) {
						SDL_Window* windowHandle = SDL_GetWindowFromID(event.window.windowID);
						int logicalWidth = 0, logicalHeight = 0;
						SDL_GetWindowSize(windowHandle, &logicalWidth, &logicalHeight);
						gfxDevice_->width_ = logicalWidth;
						gfxDevice_->height_ = logicalHeight;
						gfxDevice_->isFullscreen_ = (SDL_GetWindowFlags(windowHandle) & SDL_WINDOW_FULLSCREEN) != 0;
						// Query the pixel size the way the active backend measures it, then resize the backend
						// swap chain to match (no-op on OpenGL / software); see the SDL2 branch for the rationale
						SdlGfxDevice::queryDrawableSize(windowHandle, logicalWidth, logicalHeight,
							gfxDevice_->drawableWidth_, gfxDevice_->drawableHeight_);
						RHI::Device::ResizeSwapchain(gfxDevice_->drawableWidth_, gfxDevice_->drawableHeight_);
						ResizeScreenViewport(gfxDevice_->drawableWidth_, gfxDevice_->drawableHeight_);
					}
					break;
				case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
					// SDL only turns a close request into SDL_EVENT_QUIT when the closed window is the LAST one, which
					// stops being true as soon as another window exists (ImGui opens one per panel dragged out of the
					// main window), so the main window's close button is honored here. The event still falls through
					// to the input manager below, which is how ImGui closes a window of its own.
					if (SdlGfxDevice::isMainWindow(event.window.windowID) && SdlInputManager::shouldQuitOnRequest()) {
						shouldQuit_ = true;
					}
					DEATH_FALLTHROUGH
				default:
					if (event.type >= SDL_EVENT_DISPLAY_FIRST && event.type <= SDL_EVENT_DISPLAY_LAST) {
						gfxDevice_->updateMonitors();
					} else if (appCfg_.withGraphics) {
						SdlInputManager::parseEvent(event);
					}
					break;
			}
		}
	}
#elif defined(WITH_SDL2)
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
				case SDL_WINDOWEVENT: {
					if (SdlGfxDevice::isMainWindow(event.window.windowID)) {
						switch (event.window.event) {
							case SDL_WINDOWEVENT_FOCUS_GAINED:
								SetFocus(true);
								break;
							case SDL_WINDOWEVENT_FOCUS_LOST:
								SetFocus(false);
								break;
							case SDL_WINDOWEVENT_CLOSE:
								// SDL only turns a close into SDL_QUIT when the closed window is the LAST one, which
								// stops being true as soon as another window exists (ImGui opens one per panel
								// dragged out of the main window), so the main window's close button is honored here
								if (SdlInputManager::shouldQuitOnRequest()) {
									shouldQuit_ = true;
								}
								break;
							case SDL_WINDOWEVENT_SIZE_CHANGED: {
								gfxDevice_->width_ = event.window.data1;
								gfxDevice_->height_ = event.window.data2;
								SDL_Window* windowHandle = SDL_GetWindowFromID(event.window.windowID);
								gfxDevice_->isFullscreen_ = (SDL_GetWindowFlags(windowHandle) & SDL_WINDOW_FULLSCREEN) != 0;
								// Query the pixel size the way the active backend measures it, then resize the
								// backend swap chain to match (no-op on OpenGL / software). The explicit resize
								// is deterministic on Vulkan: some drivers never report OUT_OF_DATE for a
								// window/swap-chain size mismatch, so relying on the present path alone would
								// leave the swap chain stuck at the old size; ResizeSwapchain re-queries the
								// surface caps for the authoritative extent (this value is the hint/fallback).
								SdlGfxDevice::queryDrawableSize(windowHandle, event.window.data1, event.window.data2,
									gfxDevice_->drawableWidth_, gfxDevice_->drawableHeight_);
								RHI::Device::ResizeSwapchain(gfxDevice_->drawableWidth_, gfxDevice_->drawableHeight_);
								ResizeScreenViewport(gfxDevice_->drawableWidth_, gfxDevice_->drawableHeight_);
								break;
							}
						}
					}
					// Unlike every other event type, these were not reaching the input manager at all - and ImGui
					// drives its platform windows off them (closing, moving, focusing, entering one), so a window
					// it owns could not even be closed. Nothing in the engine's own input handling reads them.
					if (appCfg_.withGraphics) {
						SdlInputManager::parseEvent(event);
					}
					break;
				}
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
