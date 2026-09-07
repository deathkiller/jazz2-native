#if defined(WITH_CTR)

#include "CtrPlatform.h"
#include "../../../Main.h"

#include <cstdio>
#include <cstring>
#include <malloc.h>

#include <3ds.h>
#if defined(WITH_CURL) || defined(WITH_ONLINE_MULTIPLAYER)
#	include <arpa/inet.h>
#	include <unistd.h>
#endif

#include <Containers/ArrayView.h>

// libctru reads this before main() to size the main thread's stack. Its default is 32 KB, which is meant for
// a homebrew tool rather than for this engine: the asset conversion and the level loader run deep call
// chains with sizeable frames (the copy buffers alone were moved to the heap for the consoles with 64 KB
// stacks), and the parallel initialization runs the same code on worker threads that get 128 KB each (see
// Thread::Run). One megabyte out of a 64 MB application region costs nothing anyone would notice and turns
// a stack overflow - which on this console is a silent data abort with no trace - into a non-event.
extern "C" u32 __stacksize__ = 1024 * 1024;

// libctru splits the application memory region between the two heaps before main() and its default is wrong
// for this engine on the smaller consoles. The default splits what is free in half, then caps the
// application heap at 24 MB "to prefer growing the linear heap", and finally caps the linear heap at 32 MB -
// so on an Old 3DS / 2DS, whose application region is 64 MB, the linear heap takes its full 32 MB and the
// application heap is left with the ~27 MB remainder. That is the wrong way round here: the linear heap is
// only what the GPU reads out of (the framebuffers, the command buffer, the vertex arenas and the texture
// stores, see PicaTexture) and measures 3.3 MB in the menu and 5.3 MB in a level, while the application heap
// carries everything else - the level, the actors, the asset conversion that builds the cache, a downloaded
// tileset, a music module - and 27 MB of it is not enough: it runs out during the on-device cache build
// (zlib answers Z_MEM_ERROR) and again a few multiplayer levels into a session. A New 3DS hides this
// completely, because its larger region leaves the application heap ~87 MB.
//
// 12 MB is a bit over twice the measured peak, and the two heaps fail very differently, which is why the
// margin sits on this side: linear memory running out is a logged `Out of linear memory allocating a ... GPU
// store` and a texture without one, while the application heap running out is a failed `operator new`, which
// under -fno-exceptions is an immediate abort (see ReportFailedAllocation). Leaving `__ctru_heap_size` at 0
// hands the whole remainder to the application heap - ~47 MB on an Old 3DS. CtrPlatform::LogMemoryStatus()
// traces both every ten seconds, so a session's log says whether this is still the right split.
extern "C" u32 __ctru_linear_heap_size = 12 * 1024 * 1024;

using namespace Death;
using namespace Death::Containers;

namespace nCine::Backends
{
	namespace
	{
		// Often enough to see a level load move the numbers, rarely enough that a long session's log stays
		// readable; the trace itself allocates nothing that would show up in what it reports
		constexpr std::uint32_t MemoryLogIntervalMs = 10 * 1000;
	}

	bool CtrPlatform::_initialized = false;
	bool CtrPlatform::_bootConsoleQuiet = false;
	bool CtrPlatform::_isNew3DS = false;
	void* CtrPlatform::_socketBuffer = nullptr;
	std::uint32_t CtrPlatform::_lastMemoryLogTicks = 0;

#if defined(WITH_CURL) || defined(WITH_ONLINE_MULTIPLAYER)
	namespace
	{
		// The BSD sockets service (soc:u) works out of a buffer the application lends it for the life of the
		// process: it is mapped into the service's own process, which is why it has to be page-aligned and a
		// multiple of the page size, and why nothing else may touch it afterwards. 1 MB is libctru's own
		// recommendation (SOC_BUFFERSIZE in the examples) - the buffer holds every socket's send and receive
		// windows, and the ENet host asks for 256 KB of each on its one socket. Brought up once and kept until
		// Shutdown(): it comes out of the application heap, not the linear heap the textures compete for.
		constexpr std::size_t SocketBufferAlignment = 0x1000;
		constexpr std::size_t SocketBufferSize = 0x100000;
	}
#endif

	bool CtrPlatform::Initialize()
	{
		if (_initialized) {
			return true;
		}

		// The framebuffers of both screens are allocated from the linear heap and NOT from VRAM, even though
		// the 6 MB of it would hold them comfortably. CPU access to VRAM is granted by the process exheader,
		// and a homebrew process does not get write permission: the boot console below draws its glyphs with
		// the CPU straight into the framebuffer, so a VRAM framebuffer turns the very first character into a
		// data abort (permission fault, section) inside consoleInit() - before there is a log file or even a
		// visible console to report it with. The GPU is not affected, because it reaches VRAM by physical
		// address rather than through the process' page tables, which is why the render targets still live
		// there (see PicaTexture::SetRenderTarget) and why keeping the roughly 1 MB of framebuffers out of
		// VRAM leaves more of it for them. 16-bit output is all the panels can show and it halves what the
		// display transfer has to move every frame; the renderer draws into a 16-bit target as well (see
		// PicaDevice::InitializePica). No stereoscopic 3D: the game renders one view, and the parallax
		// barrier would only dim it.
		gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
		gfxSet3D(false);

		// Early boot console on the bottom screen: startup messages (including all trace messages) are shown
		// there until the renderer takes the top screen over, so a crash during initialization is visible
		// without an emulator log or a debugger attached. The screen stays a console afterwards - the game
		// has nothing to draw on it - and keeps showing warnings and errors (see WriteBootConsole).
		consoleInit(GFX_BOTTOM, nullptr);
		std::printf("Application starting...\n");

		// A New 3DS runs at 268 MHz for compatibility until an application asks for its 804 MHz clock and
		// the L2 cache; the call is a no-op on an Old 3DS. Asked for as early as possible, because
		// everything from here on - the content loading in particular - is timed against the CPU.
		bool isNew = false;
		APT_CheckNew3DS(&isNew);
		_isNew3DS = isNew;
		if (_isNew3DS) {
			osSetSpeedupEnable(true);
		}

		_initialized = true;

#if defined(WITH_CURL) || defined(WITH_ONLINE_MULTIPLAYER)
		// Before Init() reaches the event handler, whose OnInit() starts the update check - the first thing
		// that opens a socket. A console that is not on a network still brings the service up (the sockets
		// then fail to connect, which the callers already handle), so this is not where connectivity is
		// decided; the WLAN is the system's business on this console, unlike on the PSP.
		_socketBuffer = ::memalign(SocketBufferAlignment, SocketBufferSize);
		if (_socketBuffer != nullptr) {
			const ::Result rc = socInit(static_cast<u32*>(_socketBuffer), SocketBufferSize);
			if (R_FAILED(rc)) {
				LOGE("Cannot initialize the socket service (0x{:.8x}), the network will be unavailable", std::uint32_t(rc));
				std::free(_socketBuffer);
				_socketBuffer = nullptr;
			} else {
				// gethostid() is libctru's spelling of "the console's own IPv4 address", 0.0.0.0 when offline
				struct in_addr address;
				address.s_addr = std::uint32_t(::gethostid());
				LOGI("Network: Socket service initialized with a {} KB buffer, address {}", SocketBufferSize / 1024, ::inet_ntoa(address));
			}
		} else {
			LOGE("Cannot allocate the {} KB socket service buffer, the network will be unavailable", SocketBufferSize / 1024);
		}
#endif

		// How much of the application memory region the heaps got (libctru splits it before main(); the
		// linear heap is what every texture and vertex buffer comes out of, see PicaTexture) - the first
		// thing to look at when a level fails to load
		LOGI("System: {}, {} MB application memory ({} MB heap, {} MB linear heap), firmware {}.{}.{}",
			_isNew3DS ? "New Nintendo 3DS" : "Nintendo 3DS", osGetMemRegionSize(MEMREGION_APPLICATION) / (1024 * 1024),
			envGetHeapSize() / (1024 * 1024), envGetLinearHeapSize() / (1024 * 1024),
			GET_VERSION_MAJOR(osGetFirmVersion()), GET_VERSION_MINOR(osGetFirmVersion()), GET_VERSION_REVISION(osGetFirmVersion()));
		return true;
	}

	bool CtrPlatform::Update()
	{
		// Every ten seconds, so a session's log carries the trend of both heaps without the line itself
		// becoming the noise (see LogMemoryStatus)
		const std::uint32_t now = std::uint32_t(osGetTime());
		if (_lastMemoryLogTicks == 0 || now - _lastMemoryLogTicks >= MemoryLogIntervalMs) {
			_lastMemoryLogTicks = now;
			LogMemoryStatus("periodic");
		}

		// Handles the APT events - the HOME menu, sleep mode when the lid closes, the power button - and
		// returns false once the system wants the application gone
		return aptMainLoop();
	}

	void CtrPlatform::LogMemoryStatus(const char* reason)
	{
		// `uordblks` is what newlib has actually handed out, `arena` what it has taken from the heap region
		// libctru gave it: the gap between them is free-list the allocator kept, which a large request can
		// still fail to fit into, and the gap between `arena` and the region is what it can still grow by
		const struct ::mallinfo info = ::mallinfo();
		const std::uint32_t heapSize = std::uint32_t(envGetHeapSize());
		const std::uint32_t linearSize = std::uint32_t(envGetLinearHeapSize());
		const std::uint32_t linearFree = std::uint32_t(linearSpaceFree());
		LOGI("Memory ({}): heap {} KB used of {} KB ({} KB reserved by the allocator, {} KB never touched), "
			"linear {} KB used of {} KB, VRAM {} KB free",
			reason, std::uint32_t(info.uordblks) / 1024, heapSize / 1024, std::uint32_t(info.arena) / 1024,
			(heapSize - std::uint32_t(info.arena)) / 1024, (linearSize - linearFree) / 1024, linearSize / 1024,
			std::uint32_t(vramSpaceFree()) / 1024);
	}

	void CtrPlatform::Shutdown()
	{
		if (!_initialized) {
			return;
		}
		_initialized = false;
#if defined(WITH_CURL) || defined(WITH_ONLINE_MULTIPLAYER)
		if (_socketBuffer != nullptr) {
			// Every socket has been closed with its owner by now (the devices and the network manager are gone)
			socExit();
			std::free(_socketBuffer);
			_socketBuffer = nullptr;
		}
#endif
		gfxExit();
	}

	bool CtrPlatform::IsNetworkAvailable()
	{
		return (_socketBuffer != nullptr);
	}

	void CtrPlatform::WriteBootConsole(const char* text, std::int32_t length, bool important)
	{
		if (!_initialized || (_bootConsoleQuiet && !important)) {
			return;
		}
		std::fwrite(text, 1, std::size_t(length), stdout);
		std::fflush(stdout);
	}

	void CtrPlatform::SetBootConsoleQuiet(bool quiet)
	{
		_bootConsoleQuiet = quiet;
	}

	String CtrPlatform::GetDeviceName()
	{
		// The user name from the system settings: block 0x000A0000 of the config savegame is the profile,
		// whose first 0x1C bytes are the name as UTF-16 (up to 10 characters and a terminator). The service
		// is opened for the query only.
		String result;
		if (R_SUCCEEDED(cfguInit())) {
			std::uint16_t name[14] {};
			if (R_SUCCEEDED(CFGU_GetConfigInfoBlk2(sizeof(name), 0x000A0000, name))) {
				char sanitized[sizeof(name) / sizeof(name[0])]; std::size_t length = 0;
				for (std::size_t i = 0; i < arraySize(name) && name[i] != 0; i++) {
					if (name[i] >= 0x20 && name[i] < 0x7F) {
						sanitized[length++] = (char)name[i];
					}
				}
				result = StringView(sanitized, length).trimmed();
			}
			cfguExit();
		}
		if (result.empty()) {
			// A console with no usable name is told apart by its unique ID, which every console has
			if (R_SUCCEEDED(psInit())) {
				std::uint32_t deviceId = 0;
				if (R_SUCCEEDED(PS_GetDeviceId(&deviceId)) && deviceId != 0) {
					result = format("{:.8x}", deviceId);
				}
				psExit();
			}
		}
		return result;
	}

	bool CtrPlatform::IsNew3DS()
	{
		return _isNew3DS;
	}
}

#endif
