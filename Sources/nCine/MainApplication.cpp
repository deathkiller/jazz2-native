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
#elif defined(WITH_PS2)
#	include "Backends/Ps2/Ps2GfxDevice.h"
#	include "Backends/Ps2/Ps2InputManager.h"
#elif defined(WITH_PS3)
#	include "Backends/Ps3/Ps3GfxDevice.h"
#	include "Backends/Ps3/Ps3InputManager.h"
#elif defined(WITH_N64)
#	include "Backends/N64/N64GfxDevice.h"
#	include "Backends/N64/N64InputManager.h"
#endif

// For resizing the swap chain when the window size changes (the call is uniform across the backends;
// it is a no-op on the OpenGL and software backends, which have no backend-owned swap chain)
#include "Graphics/RHI/Rhi.h"

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/emscripten.h>
#elif defined(DEATH_TARGET_SWITCH)
#	include <switch.h>
#elif defined(DEATH_TARGET_DREAMCAST)
#	include <kos.h>
#elif defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
#	include <gccore.h>
#	include <fat.h>
#	include <ogc/pad.h>
#	include <sys/iosupport.h>
#	if defined(DEATH_TARGET_WII)
#		include <wiiuse/wpad.h>
#	endif
#elif defined(DEATH_TARGET_N64)
#	include <libdragon.h>
#elif defined(DEATH_TARGET_PS2)
extern "C" {
#	include <kernel.h>
#	include <sifrpc.h>
#	include <loadfile.h>
#	include <libcdvd.h>
#	include <libmc.h>
}
#	include <cstdio>
#elif defined(DEATH_TARGET_PSP)
#	include <pspkernel.h>
#	include <pspdebug.h>
#	include <psppower.h>
#elif defined(DEATH_TARGET_VITA)
#	include <vitasdk.h>
#	include <vitaGL.h>
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
#if (defined(WITH_SDL2) || defined(WITH_SDL3)) || defined(WITH_GLFW) || defined(WITH_QT5) || defined(WITH_OGC) || defined(WITH_DC) || defined(WITH_PSP) || defined(WITH_PS2) || defined(WITH_PS3) || defined(WITH_N64)
using namespace nCine::Backends;
#endif

#if defined(DEATH_TARGET_WINDOWS_RT)
#	error "For DEATH_TARGET_WINDOWS_RT, UwpApplication should be used instead of MainApplication"
#endif

#if defined(DEATH_TARGET_N64)
// libdragon's `debug.h` hides the whole logging API behind `#ifndef NDEBUG`: in a release build
// `debug_init_usblog()` and `debug_init_emulog()` are macros that expand to `false` without calling
// anything, and `debugf()` expands to nothing at all. The FUNCTIONS are still there - libdragon itself
// is not built with our NDEBUG, so libdragon.a exports them - and they are the only thing that installs
// the stderr sink (`hook_stdio_calls`), so a release build that goes through the macros gets no log
// channel whatsoever and silently drops every trace message. Declaring them directly bypasses the
// macros; the `#undef`s are needed because the macros would otherwise eat these declarations too.
#	undef debug_init_usblog
#	undef debug_init_emulog
extern "C" bool debug_init_usblog(void);
extern "C" bool debug_init_emulog(void);

static bool N64DebugInitUsbLog() { return debug_init_usblog(); }
static bool N64DebugInitEmuLog() { return debug_init_emulog(); }
#endif

#if defined(DEATH_TARGET_PS2)
/**
	@brief Writes a line straight to the EE's serial port

	The PlayStation 2 arm of Application::Init() brings up the I/O stack before anything else exists,
	including the trace system, so its results have nowhere to go through the ordinary channels. This is the
	same port Application::OnTraceReceived writes to once tracing is up, which is what an emulator shows in
	its console and what a serial cable carries on real hardware.
*/
static void earlyPrintPs2(const char* text)
{
	volatile std::uint8_t* const sioTx = reinterpret_cast<volatile std::uint8_t*>(0x1000F180);
	for (const char* c = text; *c != '\0'; c++) {
		*sioTx = std::uint8_t(*c);
	}
}
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
#elif defined(DEATH_TARGET_N64)
		// Route stderr to the emulator log channel (ISViewer/emux, shown in the Ares log) and to the
		// flashcart's USB log first: this is what installs libdragon's stderr sink, so it has to happen
		// before the first trace message is written. Both are called through N64DebugInit* rather than
		// the libdragon macros - see the declarations above for why a release build needs that.
		//
		// Then the early boot console comes up on the framebuffer, so startup messages are ALSO shown
		// directly on the screen and a crash is visible without any host tool attached. Its hook only
		// takes the stdout slot, so it neither replaces nor disables the stderr channel installed above
		// (and `console_close()` in N64GfxDevice only gives that one slot back).
		const bool n64EmuLog = N64DebugInitEmuLog();
		const bool n64UsbLog = N64DebugInitUsbLog();
		console_init();
		console_set_debug(true);
		printf("Application starting...\n");
		// Which log channels answered decides whether any trace can leave the console at all, so the
		// answer goes on the boot console - the one sink that is known to work at this point
		printf("Trace channels: emulator log %s, USB log %s\n", n64EmuLog ? "yes" : "no", n64UsbLog ? "yes" : "no");

		// The game does not fit in the 4 MB of a base console, so the Expansion Pak is a hard
		// requirement rather than a preference. Claiming it here turns a missing one into libdragon's
		// own "expansion pak required" screen at startup, instead of an allocation failure somewhere
		// deep in content loading, and it silences the advisory the allocator prints on the way past
		// 4 MB. The size goes on the boot console next to it, because a heap smaller than expected
		// explains most of what can go wrong afterwards.
		assert_memory_expanded();
		printf("Memory size: %d KB\n", get_memory_size() / 1024);

		// The cartridge filesystem holds the game content, and ContentResolver opens files while the
		// application is still being constructed - earlier than any backend's constructor runs
		if (dfs_init(DFS_DEFAULT_LOCATION) != DFS_ESUCCESS) {
			// Without the DFS image there is no game content, so halt with a readable message instead of
			// crashing on the missing files later
			printf("\n  Cannot mount the cartridge filesystem!\n\n  The ROM was built without the attached game files.\n");
			while (true) { }
		}
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
#elif defined(DEATH_TARGET_PS2)
		// The CDVD device is not registered by default, so any "cdrom0:" path fails with EPERM until its IOP
		// side is brought up - and ContentResolver opens one while the application is still being constructed,
		// which is earlier than any backend's constructor runs. So it happens here, before Init().
		//
		// CDVDMAN is normally already resident, but loading it is harmless and makes the sequence independent
		// of what the BIOS happened to leave behind; CDVDFSV is what actually serves file reads to the EE.
		SifInitRpc(0);
		SifLoadModule("rom0:CDVDMAN", 0, nullptr);
		SifLoadModule("rom0:CDVDFSV", 0, nullptr);
		sceCdInit(SCECdINIT);

		// Wait for the disc to be ready before anything tries to read a module off it
		while (sceCdDiskReady(0) != SCECdComplete) { }

		// The BIOS "cdrom0:" handler is not reachable through the newlib port's open() - it answers ENODEV - so
		// the disc is mounted as a filesystem by cdfs, which registers a "cdfs:" device with the ORIGINAL
		// `ioman` - the same I/O manager newlib's POSIX calls reach. (Loading iomanX/fileXio alongside it was
		// tried and is wrong: those route the POSIX calls to the *other* manager, where "cdfs" does not exist.)
		// The module is loaded from the disc itself, which works because SifLoadModule() goes through the IOP's
		// loadfile service rather than through the EE's file I/O - the reason this is not a chicken-and-egg.
		{
			static const char* const ps2Modules[] = {
				"cdrom0:\\CDFS.IRX;1"
			};
			for (const char* modulePath : ps2Modules) {
				const int moduleId = SifLoadModule(modulePath, 0, nullptr);
				char message[160];
				std::snprintf(message, sizeof(message), "[ps2] SifLoadModule(\"%s\") = %d\n", modulePath, moduleId);
				earlyPrintPs2(message);
			}
		}

		// The memory card is the only writable storage on this console, and once its IOP side is up it is an
		// ordinary path: MCMAN registers a "mc" device with the SAME original `ioman` the newlib port's
		// open() reaches, exactly as cdfs does above, so "mc0:/..." needs no special-case I/O anywhere. It
		// has to be brought up here rather than lazily, because PreferencesCache::Initialize() chooses the
		// config path during pre-initialization, before any backend exists to do it.
		//
		// SIO2MAN drives the controller ports, which the memory cards and the pads both hang off, so it goes
		// first - and it is loaded HERE ONLY. Ps2InputManager used to load it as well; a device driver that
		// registers twice fails the second time, and which of the two got the working one depended on
		// construction order.
		SifLoadModule("rom0:SIO2MAN", 0, nullptr);
		SifLoadModule("rom0:MCMAN", 0, nullptr);
		SifLoadModule("rom0:MCSERV", 0, nullptr);

		// Loading the modules is not enough to reach a card. `mcInit()` is libmc's handshake with MCSERV, and
		// nothing - not even MCMAN's own ioman entry points - answers for a slot before it has been probed
		// through that: an mkdir on a perfectly good card comes back ENOENT until then. The probe itself, and
		// the choice of which slot to save on, belong to PreferencesCache and are done there.
		mcInit(MC_TYPE_MC);
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
#elif defined(DEATH_TARGET_VITA)
		// Enable analog sampling for controllers
		sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
		sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

		// Enabling sampling for the analogs
		sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
#elif defined(DEATH_TARGET_DREAMCAST)
		// Early boot log on the framebuffer console: startup messages (including all trace messages)
		// are shown directly on the screen, so crashes are visible without a serial cable attached;
		// DcGfxDevice switches dbgio back to the serial port when the real renderer takes over
		dbgio_dev_select("fb");
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
		while (!app._shouldQuit) {
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

	bool MainApplication::IsScreenKeyboardVisible()
	{
#if defined(DEATH_TARGET_WINDOWS)
		HWND hwnd = ::FindWindowEx(NULL, NULL, L"IPTip_Main_Window", NULL);
		return (hwnd != NULL && ::IsWindowVisible(hwnd));
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
		_profileStartTime = TimeStamp::now();
#endif
		_wasSuspended = ShouldSuspend();

		// Only `OnPreInit()` can modify the application configuration
		if (argc > 1) {
#if defined(DEATH_TARGET_WINDOWS)
			_appCfg._argv = Array<String>(ValueInit, argc - 1);
			for (std::int32_t i = 1; i < argc; i++) {
				_appCfg._argv[i - 1] = Utf8::FromUtf16(argv[i]);
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
				_appCfg._argv = Array<StringView>(ValueInit, count);
				count = 0;
				i = 1;
				while (i < argc) {
					const char* arg = argv[i];
					if (arg[0] == '-' && arg[1] == 'N' && arg[2] == 'S') {
						i += 2; // Skip key and value
					} else {
						_appCfg._argv[count++] = arg;
						i++;
					}
				}
			}
#else
			_appCfg._argv = Array<StringView>(ValueInit, argc - 1);
			for (std::int32_t i = 1; i < argc; i++) {
				_appCfg._argv[i - 1] = argv[i];
			}
#endif
		}

		PreInitCommon(createAppEventHandler());

		if (_shouldQuit) {
			// If the app was quit from OnPreInit(), skip further initialization
			return;
		}

#if defined(DEATH_DEBUG)
#	define INIT_MESSAGE_SUFFIX " in debug configuration"
#else
#	define INIT_MESSAGE_SUFFIX ""
#endif

		if (_appCfg.withGraphics) {
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
			IGfxDevice::ContextInfo contextInfo(_appCfg);
			const DisplayMode::VSync vSyncMode = (_appCfg.withVSync ? DisplayMode::VSync::Enabled : DisplayMode::VSync::Disabled);
#if defined(RHI_USE_FB16) && defined(WITH_RHI_GL)
			// 16-bit screen framebuffer (`NCINE_RHI_USE_FB16`): ask the window system for a 5/6/5 visual with no
			// destination alpha, halving the memory and the present bandwidth of the default framebuffer. The
			// render targets follow through Texture::ColorTargetFormat. The depth/stencil request is unchanged -
			// the 2D pipeline never allocates them for the screen anyway, and a driver is free to pick a deeper
			// buffer than asked for, so this is a request rather than a guarantee.
			DisplayMode displayMode(5, 6, 5, 0, 24, 8, DisplayMode::DoubleBuffering::Enabled, vSyncMode);
#else
			DisplayMode displayMode(8, 8, 8, 8, 24, 8, DisplayMode::DoubleBuffering::Enabled, vSyncMode);
#endif

			const IGfxDevice::WindowMode windowMode(_appCfg.resolution.X, _appCfg.resolution.Y, _appCfg.windowPosition.X,
				_appCfg.windowPosition.Y, _appCfg.fullscreen, _appCfg.resizable, _appCfg.windowScaling);

#if (defined(WITH_SDL2) || defined(WITH_SDL3))
			_gfxDevice = std::make_unique<SdlGfxDevice>(windowMode, contextInfo, displayMode);
			_inputManager = std::make_unique<SdlInputManager>();
#elif defined(WITH_GLFW)
			_gfxDevice = std::make_unique<GlfwGfxDevice>(windowMode, contextInfo, displayMode);
			_inputManager = std::make_unique<GlfwInputManager>();
#elif defined(WITH_QT5)
			FATAL_ASSERT_MSG(_qt5Widget, "The Qt5 widget has not been assigned");
			_gfxDevice = std::make_unique<Qt5GfxDevice>(windowMode, contextInfo, displayMode, *_qt5Widget);
			_inputManager = std::make_unique<Qt5InputManager>(*_qt5Widget);
#elif defined(WITH_N64)
			_gfxDevice = std::make_unique<N64GfxDevice>(windowMode, contextInfo, displayMode);
			_inputManager = std::make_unique<N64InputManager>();
#elif defined(WITH_OGC)
			_gfxDevice = std::make_unique<OgcGfxDevice>(windowMode, contextInfo, displayMode);
			_inputManager = std::make_unique<OgcInputManager>();
#elif defined(WITH_DC)
			_gfxDevice = std::make_unique<DcGfxDevice>(windowMode, contextInfo, displayMode);
			_inputManager = std::make_unique<DcInputManager>();
#elif defined(WITH_PSP)
			_gfxDevice = std::make_unique<PspGfxDevice>(windowMode, contextInfo, displayMode);
			_inputManager = std::make_unique<PspInputManager>();
#elif defined(WITH_PS2)
			_gfxDevice = std::make_unique<Ps2GfxDevice>(windowMode, contextInfo, displayMode);
			_inputManager = std::make_unique<Ps2InputManager>();
#elif defined(WITH_PS3)
			_gfxDevice = std::make_unique<Ps3GfxDevice>(windowMode, contextInfo, displayMode);
			_inputManager = std::make_unique<Ps3InputManager>();
#endif
			_gfxDevice->setWindowTitle(_appCfg.windowTitle.data());
			if (!_appCfg.windowIconFilename.empty()) {
				String windowIconFilePath = fs::CombinePath(GetDataPath(), _appCfg.windowIconFilename);
				if (fs::IsReadableFile(windowIconFilePath)) {
					_gfxDevice->setWindowIcon(windowIconFilePath);
				}
			}
		} else {
			LOGI(NCINE_APP_NAME " v" NCINE_VERSION " initializing" INIT_MESSAGE_SUFFIX "...");

			_gfxDevice = std::make_unique<NullGfxDevice>();
			_inputManager = std::make_unique<NullInputManager>();
		}

#if defined(NCINE_PROFILING)
		_timings[(std::int32_t)Timings::PreInit] = _profileStartTime.secondsSince();
#endif
#if !defined(WITH_QT5)
		// Common initialization on Qt5 is performed later, when OpenGL can be used
		InitCommon();
#endif
	}

	void MainApplication::ProcessStep()
	{
		if (_appCfg.withGraphics) {
#if defined(WITH_GLFW) || (defined(WITH_SDL2) || defined(WITH_SDL3))
			ProcessEvents();
#elif defined(WITH_QT5GAMEPAD)
			static_cast<Qt5InputManager&>(*_inputManager).updateJoystickStates();
#elif defined(WITH_N64)
			// No window events on a console; polling the joypad ports is the whole event pump
			N64InputManager::updateJoystickStates();
#elif defined(WITH_OGC)
			// No window events on a console; polling the controller ports is the whole event pump
			OgcInputManager::updateJoystickStates();
#elif defined(WITH_DC)
			// No window events on a console; polling the maple bus is the whole event pump
			DcInputManager::updateJoystickStates();
#elif defined(WITH_PSP)
			// No window events on a console; polling the controller service is the whole event pump
			PspInputManager::updateJoystickStates();
#elif defined(WITH_PS2)
			// No window events on a console; polling the pad ports is the whole event pump
			Ps2InputManager::updateJoystickStates();
#elif defined(WITH_PS3)
			// The one console here that DOES have window events: the XMB can ask the game to quit or tell it
			// that the user opened the in-game menu, and those arrive on the sysutil callback queue that
			// updateJoystickStates() drains alongside polling the pads
			Ps3InputManager::updateJoystickStates();
#endif
		}

		const bool suspended = ShouldSuspend();
		if (_wasSuspended != suspended) {
			if (suspended) {
				Suspend();
			} else {
				Resume();
			}
			_wasSuspended = suspended;
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
						_shouldQuit = true;
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
						_gfxDevice->_width = logicalWidth;
						_gfxDevice->_height = logicalHeight;
						_gfxDevice->_isFullscreen = (SDL_GetWindowFlags(windowHandle) & SDL_WINDOW_FULLSCREEN) != 0;
						// Query the pixel size the way the active backend measures it, then resize the backend
						// swap chain to match (no-op on OpenGL / software); see the SDL2 branch for the rationale
						SdlGfxDevice::queryDrawableSize(windowHandle, logicalWidth, logicalHeight,
							_gfxDevice->_drawableWidth, _gfxDevice->_drawableHeight);
						RHI::Device::ResizeSwapchain(_gfxDevice->_drawableWidth, _gfxDevice->_drawableHeight);
						ResizeScreenViewport(_gfxDevice->_drawableWidth, _gfxDevice->_drawableHeight);
					}
					break;
				case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
					// SDL only turns a close request into SDL_EVENT_QUIT when the closed window is the LAST one, which
					// stops being true as soon as another window exists (ImGui opens one per panel dragged out of the
					// main window), so the main window's close button is honored here. The event still falls through
					// to the input manager below, which is how ImGui closes a window of its own.
					if (SdlGfxDevice::isMainWindow(event.window.windowID) && SdlInputManager::shouldQuitOnRequest()) {
						_shouldQuit = true;
					}
					DEATH_FALLTHROUGH
				default:
					if (event.type >= SDL_EVENT_DISPLAY_FIRST && event.type <= SDL_EVENT_DISPLAY_LAST) {
						_gfxDevice->updateMonitors();
					} else if (_appCfg.withGraphics) {
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
						_shouldQuit = true;
					}
					break;
				case SDL_DISPLAYEVENT:
					_gfxDevice->updateMonitors();
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
									_shouldQuit = true;
								}
								break;
							case SDL_WINDOWEVENT_SIZE_CHANGED: {
								_gfxDevice->_width = event.window.data1;
								_gfxDevice->_height = event.window.data2;
								SDL_Window* windowHandle = SDL_GetWindowFromID(event.window.windowID);
								_gfxDevice->_isFullscreen = (SDL_GetWindowFlags(windowHandle) & SDL_WINDOW_FULLSCREEN) != 0;
								// Query the pixel size the way the active backend measures it, then resize the
								// backend swap chain to match (no-op on OpenGL / software). The explicit resize
								// is deterministic on Vulkan: some drivers never report OUT_OF_DATE for a
								// window/swap-chain size mismatch, so relying on the present path alone would
								// leave the swap chain stuck at the old size; ResizeSwapchain re-queries the
								// surface caps for the authoritative extent (this value is the hint/fallback).
								SdlGfxDevice::queryDrawableSize(windowHandle, event.window.data1, event.window.data2,
									_gfxDevice->_drawableWidth, _gfxDevice->_drawableHeight);
								RHI::Device::ResizeSwapchain(_gfxDevice->_drawableWidth, _gfxDevice->_drawableHeight);
								ResizeScreenViewport(_gfxDevice->_drawableWidth, _gfxDevice->_drawableHeight);
								break;
							}
						}
					}
					// Unlike every other event type, these were not reaching the input manager at all - and ImGui
					// drives its platform windows off them (closing, moving, focusing, entering one), so a window
					// it owns could not even be closed. Nothing in the engine's own input handling reads them.
					if (_appCfg.withGraphics) {
						SdlInputManager::parseEvent(event);
					}
					break;
				}
				default:
					if (_appCfg.withGraphics) {
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

		if (_appCfg.withGraphics) {
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
