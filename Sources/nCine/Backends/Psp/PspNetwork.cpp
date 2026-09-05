#include "PspNetwork.h"

#if defined(WITH_PSP) && defined(WITH_CURL)

#include "../../../Main.h"

#include <pspkernel.h>
#include <pspsysmem.h>
#include <psputility.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>
#include <pspnet_resolver.h>
#include <pspnet_adhoc.h>
#include <pspnet_adhocctl.h>
#include <pspwlan.h>

#include <cstring>

namespace nCine::Backends
{
	// Set once the stack itself is up, so it is not torn down again when it never came up
	static bool _initialized = false;
	// Whether the infrastructure half of the stack is up (see BeginInfrastructure()), whether an access point
	// is joined on top of it (see ScopedConnection), and whether the WLAN is in ad hoc mode instead - the two
	// USES are exclusive (an access point or a group, never both), the libraries behind them are not
	static bool _inetActive = false;
	static bool _apConnected = false;
	static bool _adhocActive = false;

	// The firmware modules of the two halves are loaded once, at startup, and stay resident until shutdown -
	// the way a retail game loads every net module it will ever use at boot. Loading the ad hoc module in
	// the middle of a session, after the infrastructure one had been unloaded, killed the process on the
	// PS Vita's PSP emulator (Adrenaline) with nothing returned (2026-09-05); only the libraries on top of
	// the modules are initialized and terminated as the mode switches.
	static bool _inetModuleLoaded = false;
	static bool _adhocModuleLoaded = false;

	// The same holds for the libraries on top of the modules: re-initializing pspnet_inet after a
	// sceNetInetTerm() killed the process the same way (2026-09-05), so once a library is up it stays up
	// until Shutdown(). `_adhocActive` is then only the MODE - which of the two the WLAN is being used for -
	// and switching it is nothing but dropping the access point one way and leaving the group the other.
	static bool _adhocLibrariesActive = false;

	// How many ScopedConnection instances are alive, and the semaphore that keeps their bookkeeping and the
	// association itself in step. A semaphore and not the spinlock the shared code uses: the holder of this
	// can be inside a 15 second association, and spinning through that on a single-core console would starve
	// the very thread the waiter is waiting for.
	static std::int32_t _connectionCount = 0;
	static SceUID _connectionLock = -1;

	// The association outlives its last holder by this much, because the next one is usually seconds away -
	// leaving a game for the server list, or opening the list again - and re-associating costs seconds of
	// DHCP every time. `_lingerArmed` is what Update() tests every frame; the deadline itself is 64-bit and
	// is only read under the lock, where it cannot be seen half-updated.
	static constexpr std::int32_t ConnectionLingerSecs = 30;
	static bool _lingerArmed = false;
	static std::int64_t _lingerDeadline = 0;

	struct ConnectionLockGuard
	{
		explicit ConnectionLockGuard(bool blocking = true)
			: _locked(false)
		{
			if (_connectionLock < 0) {
				return;
			}
			_locked = (blocking
				? sceKernelWaitSema(_connectionLock, 1, nullptr) >= 0
				: sceKernelPollSema(_connectionLock, 1) >= 0);
		}

		~ConnectionLockGuard() {
			if (_locked) {
				sceKernelSignalSema(_connectionLock, 1);
			}
		}

		ConnectionLockGuard(const ConnectionLockGuard&) = delete;
		ConnectionLockGuard& operator=(const ConnectionLockGuard&) = delete;

		// Whether the caller may go ahead: it holds the semaphore, or there is no semaphore to hold - which
		// is the case when it could not be created, and running unguarded then beats never running at all
		explicit operator bool() const {
			return _locked || _connectionLock < 0;
		}

	private:
		bool _locked;
	};

	// The states sceNetAdhocctlGetState() reports; the SDK header names none of them
	static constexpr int AdhocctlStateDisconnected = 0;
	static constexpr int AdhocctlStateConnected = 1;
	static constexpr int AdhocctlStateScanning = 2;

	// Both a group name and a product code are uppercase alphanumerics and nothing else, so everything else
	// in the source text is skipped; `maxLength` is how many of them the destination holds, and the number
	// actually written is returned. No terminator is added, because the product code has no room for one.
	static std::int32_t NormalizeAdhocName(Death::Containers::StringView source, char* name, std::int32_t maxLength)
	{
		std::int32_t length = 0;
		for (char c : source) {
			if (length >= maxLength) {
				break;
			}
			if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) {
				name[length++] = c;
			} else if (c >= 'a' && c <= 'z') {
				name[length++] = (char)(c - 'a' + 'A');
			}
		}
		return length;
	}

	// None of the failures here are fatal to the application - all that is lost is the network - hence the
	// warnings rather than errors. The error codes are formatted unsigned, because they all have the high
	// bit set (0x8011....) and would print as the negative `int` the SDK returns them as.

	// Brings the infrastructure half of the stack up: the INET module, the BSD sockets it implements, the
	// resolver and the access point control. The COMMON module and sceNetInit() underneath are shared with
	// ad hoc mode and stay up either way, so they are not part of this.
	// Every module load here comes out of what the heap left free (see PSP_HEAP_THRESHOLD_SIZE_KB), and running
	// out of it is the one failure these calls really have, so the amount is logged in front of them
	static void LogFreeMemory()
	{
		LOGI("{} KB of memory free ({} KB in the largest block)",
			std::uint32_t(sceKernelTotalFreeMemSize() / 1024), std::uint32_t(sceKernelMaxFreeMemSize() / 1024));
	}

	static bool BeginInfrastructure()
	{
		if (_inetActive) {
			return true;
		}

		std::int32_t result;
		if (!_inetModuleLoaded) {
			LogFreeMemory();
			result = sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
			if (result < 0) {
				LOGW("Cannot load the infrastructure module, sceUtilityLoadNetModule() failed with error 0x{:.8x}", std::uint32_t(result));
				return false;
			}
			_inetModuleLoaded = true;
		}
		result = sceNetInetInit();
		if (result < 0) {
			LOGW("Failed to initialize the network stack, sceNetInetInit() failed with error 0x{:.8x}", std::uint32_t(result));
			return false;
		}
		// Host names are resolved through gethostbyname(), which the newlib port routes into this resolver -
		// without it every transfer fails with "Could not resolve host"
		result = sceNetResolverInit();
		if (result < 0) {
			LOGW("Failed to initialize the network stack, sceNetResolverInit() failed with error 0x{:.8x}", std::uint32_t(result));
			sceNetInetTerm();
			return false;
		}
		result = sceNetApctlInit(0x8000, 48);
		if (result < 0) {
			LOGW("Failed to initialize the network stack, sceNetApctlInit() failed with error 0x{:.8x}", std::uint32_t(result));
			sceNetResolverTerm();
			sceNetInetTerm();
			return false;
		}

		_inetActive = true;
		return true;
	}

	// Takes the infrastructure half down again, in the reverse order, so ad hoc mode can have the module slot
	// and the memory it holds
	// Drops the access point association, if there is one, and waits for the control to report it gone
	static void DisconnectAccessPoint()
	{
		if (!_inetActive) {
			return;
		}

		_apConnected = false;
		_lingerArmed = false;
		_lingerDeadline = 0;

		// Disconnecting is asynchronous - the control's own thread walks the state machine back to
		// DISCONNECTED - and terminating the library or unloading its module while that is still under way
		// pulls the code out from under that thread. So the state is polled until it is back, with a bound
		// in case the firmware never gets there (then the teardown proceeds as it always has).
		int state = PSP_NET_APCTL_STATE_DISCONNECTED;
		std::int32_t stateResult = sceNetApctlGetState(&state);
		if (stateResult >= 0 && state != PSP_NET_APCTL_STATE_DISCONNECTED) {
			sceNetApctlDisconnect();
			constexpr std::int32_t DisconnectTimeoutMs = 5000;
			constexpr std::int32_t DisconnectPollIntervalMs = 100;
			std::int32_t elapsed = 0;
			for (; elapsed < DisconnectTimeoutMs; elapsed += DisconnectPollIntervalMs) {
				if (sceNetApctlGetState(&state) < 0 || state == PSP_NET_APCTL_STATE_DISCONNECTED) {
					break;
				}
				sceKernelDelayThread(DisconnectPollIntervalMs * 1000);
			}
		}
	}

	// Takes the infrastructure libraries down; only at shutdown, see _adhocLibrariesActive
	static void EndInfrastructure()
	{
		if (!_inetActive) {
			return;
		}

		DisconnectAccessPoint();
		_inetActive = false;
		sceNetApctlTerm();
		sceNetResolverTerm();
		sceNetInetTerm();
	}

	void PspNetwork::Initialize()
	{
		// The stack lives in these firmware modules and none of them is loaded into a user-mode application
		// by default. COMMON has to go first, and the module of whichever half of the stack is in use goes
		// on top of it.
		LogFreeMemory();
		std::int32_t result = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
		if (result < 0) {
			LOGW("Cannot load the common network module, sceUtilityLoadNetModule() failed with error 0x{:.8x}", std::uint32_t(result));
			return;
		}

		// The pool is what every socket buffer is carved out of, so it also caps how much can be in flight;
		// 128 KB is what the firmware's own network applications ask for. The two priority/stack pairs
		// belong to the callout and interrupt threads the stack creates for itself.
		result = sceNetInit(128 * 1024, 42, 4 * 1024, 42, 4 * 1024);
		if (result < 0) {
			LOGW("Failed to initialize the network stack, sceNetInit() failed with error 0x{:.8x}", std::uint32_t(result));
			sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
			return;
		}

		_initialized = true;
		_connectionLock = sceKernelCreateSema("nCineNetConnection", 0, 1, 1, nullptr);

		// The ad hoc module goes first, so that a load that cannot succeed fails (or, on the PS Vita's PSP
		// emulator, dies) before anything else is in memory and is easy to tell apart in the log; it is only
		// the module, nothing of it runs until AdhocBegin(). Not having it costs only the ad hoc mode.
		LogFreeMemory();
		result = sceUtilityLoadNetModule(PSP_NET_MODULE_ADHOC);
		if (result >= 0) {
			_adhocModuleLoaded = true;
		} else {
			LOGW("Cannot load the ad hoc module, sceUtilityLoadNetModule() failed with error 0x{:.8x}", std::uint32_t(result));
		}

		// Infrastructure is the half everything but ad hoc multiplayer wants, so it is the one brought up
		// here - but only as far as its module and libraries: no access point is joined until something
		// takes a ScopedConnection. Failing it is not fatal, because ad hoc mode needs none of it.
		BeginInfrastructure();
		LogFreeMemory();
	}

	// Leaves any group and takes the ad hoc libraries down; only at shutdown, see _adhocLibrariesActive
	static void EndAdhoc()
	{
		PspNetwork::AdhocLeaveGroup();
		_adhocActive = false;
		if (!_adhocLibrariesActive) {
			return;
		}
		_adhocLibrariesActive = false;
		sceNetAdhocctlTerm();
		sceNetAdhocTerm();
	}

	void PspNetwork::Shutdown()
	{
		if (!_initialized) {
			return;
		}

		_initialized = false;
		EndAdhoc();
		EndInfrastructure();

		// The core goes down BEFORE the modules that plugged into it are unloaded - the order the SDK's own
		// samples use. The other way round killed the process on the PS Vita's PSP emulator (Adrenaline) once
		// ad hoc mode had been used in the session (2026-09-05): with the ad hoc module gone first, sceNetTerm()
		// died, and leaving the core to sceKernelExitGame() died inside that instead - the ad hoc PRXes leave
		// registrations in the core that only its termination takes back, and by then their code was gone.
		sceNetTerm();
		if (_adhocModuleLoaded) {
			_adhocModuleLoaded = false;
			sceUtilityUnloadNetModule(PSP_NET_MODULE_ADHOC);
		}
		if (_inetModuleLoaded) {
			_inetModuleLoaded = false;
			sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
		}
		sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);

		// Every thread that could hold a connection is joined before the application gets here, so the count
		// is not consulted - and a ScopedConnection outliving this finds the semaphore gone and skips the lock
		if (_connectionLock >= 0) {
			SceUID connectionLock = _connectionLock;
			_connectionLock = -1;
			sceKernelDeleteSema(connectionLock);
		}
	}

	// sceNetApctlConnect() takes no access point of its own but the index of one of the connections saved in
	// the system settings ("Network Settings" -> "Infrastructure Mode"), and refuses an index nothing is saved
	// under with 0x80110601. Emulators always answer for the first slot, a console only has one once the user
	// has created it - and not necessarily in the first slot, because deleting a connection leaves its slot
	// empty - so the slots are probed and the first one that exists is used.
	//
	// The PS Vita's PSP emulator is the exception: it has no saved connections at all, because it does not
	// connect through them - whatever the emulated console asks for is routed through the Vita's own network,
	// no matter which profile is named. So the probe finds nothing there, and the firmware's own "connect
	// automatically" profile, which is configuration 0, is what the emulator is given instead. A real console
	// with nothing saved refuses that one the same way it refuses any other, so nothing is lost by trying.
	static constexpr std::int32_t AutomaticNetworkConfiguration = 0;

	static std::int32_t FindNetworkConfiguration()
	{
		// The firmware's own settings run out of slots long before this, so it is only a stop for the search
		constexpr std::int32_t MaxNetworkConfigurations = 32;

		for (std::int32_t i = 1; i <= MaxNetworkConfigurations; i++) {
			if (sceUtilityCheckNetParam(i) == 0) {
				// The name is what the settings show the connection as, so it is the only way to tell the
				// user which one of theirs was picked
				netData name = {};
				if (sceUtilityGetNetParam(i, PSP_NETPARAM_NAME, &name) >= 0) {
					name.asString[sizeof(name.asString) - 1] = '\0';
					LOGI("Using network configuration {} (\"{}\")", i, name.asString);
				} else {
					LOGI("Using network configuration {}", i);
				}
				return i;
			}
		}
		return -1;
	}

	static bool DoEnsureConnected()
	{
		// Nothing can be joined with the WLAN switch off, and the firmware reports that as an error code like
		// any other, so it is checked here to name the one cause the user can do something about
		if (sceWlanGetSwitchState() == 0) {
			LOGW("No network connection is available, the WLAN switch is off");
			return false;
		}

		std::int32_t configuration = FindNetworkConfiguration();
		const bool automatic = (configuration < 0);
		if (automatic) {
			// Nothing saved - either a console the user never set up, or the PS Vita's emulator, which never
			// has one (see AutomaticNetworkConfiguration)
			LOGI("No network configuration is saved on this console, trying the automatic one");
			configuration = AutomaticNetworkConfiguration;
		}

		std::int32_t result = sceNetApctlConnect(configuration);
		if (result < 0) {
			// The firmware refuses a connect while its state machine is already running, which is exactly what
			// a second caller arriving during the first one's association gets (0x80410a80). That is not a
			// failure: the association it is waiting for is the one already in progress, so it polls that to
			// the same answer instead of giving up - which is what it used to do, and what left the server list
			// unresolvable for a whole discovery interval.
			int state = PSP_NET_APCTL_STATE_DISCONNECTED;
			if (sceNetApctlGetState(&state) < 0 || state == PSP_NET_APCTL_STATE_DISCONNECTED) {
				if (automatic) {
					// The one cause the user can do something about on a real console is named, the code is for
					// the case where it is something else
					LOGW("No network connection is available, no network configuration is saved on this console (sceNetApctlConnect() failed with error 0x{:.8x})", std::uint32_t(result));
				} else {
					LOGW("No network connection is available, sceNetApctlConnect() failed with error 0x{:.8x}", std::uint32_t(result));
				}
				return false;
			}
		}

		// The budget is only spent in full when the state machine keeps making progress without finishing;
		// association failures (out of range, wrong key, no DHCP answer) end back in DISCONNECTED and are
		// caught a poll or two later. PPSSPP steps its state machine on a timer and takes a little over 6
		// seconds to reach GOT_IP, so the budget cannot be much smaller than this either.
		constexpr std::int32_t ConnectionTimeoutMs = 15000;
		constexpr std::int32_t ConnectionPollIntervalMs = 100;

		bool leftDisconnected = false;
		for (std::int32_t elapsed = 0; elapsed < ConnectionTimeoutMs; elapsed += ConnectionPollIntervalMs) {
			// Plain `int` and not `std::int32_t`, because sceNetApctlGetState() takes `int*` and newlib
			// typedefs the fixed-width type to `long` on this target
			int state;
			if (sceNetApctlGetState(&state) < 0) {
				return false;
			}
			if (state == PSP_NET_APCTL_STATE_GOT_IP) {
				LOGI("Network connection is established");
				return true;
			}
			if (state == PSP_NET_APCTL_STATE_DISCONNECTED) {
				if (leftDisconnected) {
					LOGW("Failed to establish a network connection, the access point could not be joined");
					sceNetApctlDisconnect();
					return false;
				}
			} else {
				leftDisconnected = true;
			}
			sceKernelDelayThread(ConnectionPollIntervalMs * 1000);
		}

		LOGW("Timed out waiting for a network connection (sceNetApctlGetState)");
		sceNetApctlDisconnect();
		return false;
	}

	PspNetwork::ScopedConnection::ScopedConnection(bool acquire)
		: _acquired(false), _connected(false)
	{
		if (!acquire || !_initialized || _adhocActive || !_inetActive) {
			// In ad hoc mode there is no access point to join - the WLAN is busy being the network itself -
			// and without the infrastructure half of the stack there is nothing to join one with either
			return;
		}

		ConnectionLockGuard guard;

		// The count is raised whether or not the association below succeeds, so that the destructor stays
		// symmetric with this and a holder that failed does not drop an association a later holder made
		_acquired = true;
		_connectionCount++;

		// Anything still lingering from an earlier holder is this one's association now, and it is instant
		_lingerArmed = false;
		_lingerDeadline = 0;

		if (!_apConnected) {
			// More than one holder can arrive here - the update check, the server list and the transport all
			// do - and the lock is what makes the second one wait for the first one's answer instead of
			// starting a second association the firmware would refuse anyway
			_apConnected = DoEnsureConnected();
		}
		_connected = _apConnected;
	}

	PspNetwork::ScopedConnection::~ScopedConnection()
	{
		if (!_acquired) {
			return;
		}

		ConnectionLockGuard guard;

		_connectionCount--;
		if (_connectionCount <= 0) {
			_connectionCount = 0;
			if (_apConnected) {
				// Not dropped here: the next holder is usually seconds away, so the association is only given
				// a deadline and Update() is what ends it (see ConnectionLingerSecs)
				_lingerDeadline = sceKernelGetSystemTimeWide() + std::int64_t(ConnectionLingerSecs) * 1000000;
				_lingerArmed = true;
				LOGD("Nothing needs the network connection, releasing it in {} seconds", ConnectionLingerSecs);
			}
		}
	}

	void PspNetwork::Update()
	{
		if (!_lingerArmed) {
			return;
		}

		// This runs on the main thread, which must never wait: a holder can be inside a 15 second association
		// with the lock held, and the deadline is not worth a dropped frame - so it gives up and the next
		// frame tries again
		ConnectionLockGuard guard(false);
		if (!guard) {
			return;
		}

		// A holder that arrived while this was taking the lock has already disarmed the deadline
		if (!_lingerArmed || sceKernelGetSystemTimeWide() < _lingerDeadline) {
			return;
		}

		_lingerArmed = false;
		_lingerDeadline = 0;
		if (_connectionCount <= 0 && _apConnected) {
			// Disassociating is all a user-mode application can do about the radio - attaching and detaching
			// the WLAN device itself is a kernel-only interface - but it is what stops the console from
			// talking to the access point, which is what costs the battery
			sceNetApctlDisconnect();
			_apConnected = false;
			LOGI("Network connection released, nothing has needed it for {} seconds", ConnectionLingerSecs);
		}
	}

	// Waits until the ad hoc control reaches the given state, or the given number of milliseconds passed
	static bool WaitForAdhocctlState(int expected, std::int32_t timeoutMs)
	{
		constexpr std::int32_t PollIntervalMs = 100;
		for (std::int32_t elapsed = 0; elapsed < timeoutMs; elapsed += PollIntervalMs) {
			int state = -1;
			if (sceNetAdhocctlGetState(&state) < 0) {
				return false;
			}
			if (state == expected) {
				return true;
			}
			sceKernelDelayThread(PollIntervalMs * 1000);
		}
		return false;
	}

	bool PspNetwork::AdhocBegin()
	{
		if (!_initialized) {
			return false;
		}
		if (_adhocActive) {
			return true;
		}
		if (sceWlanGetSwitchState() == 0) {
			LOGW("Cannot enter ad hoc mode, the WLAN switch is off");
			return false;
		}

		// No ScopedConnection may be halfway through an association while the infrastructure half is taken
		// down underneath it - the lock is what every holder takes for the duration of its association
		if (!_adhocModuleLoaded) {
			LOGW("Cannot enter ad hoc mode, the ad hoc module is not loaded");
			return false;
		}

		ConnectionLockGuard guard;

		// The WLAN is either associated with an access point or an ad hoc peer, never both - so the
		// association goes, and nothing else: the infrastructure libraries stay initialized (see
		// _adhocLibrariesActive for why), and AdhocEnd() has nothing to bring back
		DisconnectAccessPoint();

		if (_adhocLibrariesActive) {
			_adhocActive = true;
			LOGI("Ad hoc mode entered");
			return true;
		}

		std::int32_t result = sceNetAdhocInit();
		if (result < 0) {
			LOGW("Cannot enter ad hoc mode, sceNetAdhocInit() failed with error 0x{:.8x}", std::uint32_t(result));
			return false;
		}
		// The product code is what keeps groups of this game apart from other games' on the same channel. All
		// 9 characters of it are matched, so the application name is padded out with zeroes the way a retail
		// disc code is (4 letters and 5 digits), and it has to stay the same across builds to remain joinable.
		// Static, in case the firmware keeps the pointer rather than a copy - its own thread is what uses it
		static struct productStruct product;
		std::memset(&product, 0, sizeof(product));
		product.unknown = 0;
		std::int32_t productLength = NormalizeAdhocName(NCINE_APP, product.product, std::int32_t(sizeof(product.product)));
		for (std::int32_t i = productLength; i < std::int32_t(sizeof(product.product)); i++) {
			product.product[i] = '0';
		}
		// The header suggests an 8 KB stack for the control's thread, the SDK's own game sharing sample gives
		// it 32 KB - and an overflow inside a firmware thread kills the process with nothing in the log
		result = sceNetAdhocctlInit(32 * 1024, 0x30, &product);
		if (result < 0) {
			LOGW("Cannot enter ad hoc mode, sceNetAdhocctlInit() failed with error 0x{:.8x}", std::uint32_t(result));
			// The ad hoc library is deliberately left initialized: terminating it only to initialize it
			// again on the next attempt is the very sequence that kills the process
			return false;
		}

		_adhocLibrariesActive = true;
		_adhocActive = true;
		LOGI("Ad hoc mode entered");
		return true;
	}

	void PspNetwork::AdhocEnd()
	{
		if (!_adhocActive) {
			return;
		}

		ConnectionLockGuard guard;
		AdhocLeaveGroup();
		// The libraries stay up (see _adhocLibrariesActive); the infrastructure ones never went down, so the
		// next ScopedConnection joins the access point as if nothing happened
		_adhocActive = false;
		LOGI("Ad hoc mode left");
	}

	bool PspNetwork::IsAdhocActive()
	{
		return _adhocActive;
	}

	bool PspNetwork::AdhocCreateGroup(const char* name)
	{
		if (!_adhocActive) {
			return false;
		}
		AdhocLeaveGroup();
		std::int32_t result = sceNetAdhocctlCreate(name);
		if (result < 0) {
			LOGW("Cannot create the ad hoc group \"{}\", sceNetAdhocctlCreate() failed with error 0x{:.8x}", name, std::uint32_t(result));
			return false;
		}
		if (!WaitForAdhocctlState(AdhocctlStateConnected, 15000)) {
			LOGW("Timed out creating the ad hoc group \"{}\"", name);
			sceNetAdhocctlDisconnect();
			return false;
		}
		LOGI("Ad hoc group \"{}\" created", name);
		return true;
	}

	bool PspNetwork::AdhocJoinGroup(const char* name)
	{
		if (!_adhocActive) {
			return false;
		}
		AdhocLeaveGroup();
		// Connect joins the group of that name if one is in reach and creates it otherwise, which also
		// covers a host whose group disappeared for a moment
		std::int32_t result = sceNetAdhocctlConnect(name);
		if (result < 0) {
			LOGW("Cannot join the ad hoc group \"{}\", sceNetAdhocctlConnect() failed with error 0x{:.8x}", name, std::uint32_t(result));
			return false;
		}
		if (!WaitForAdhocctlState(AdhocctlStateConnected, 15000)) {
			LOGW("Timed out joining the ad hoc group \"{}\"", name);
			sceNetAdhocctlDisconnect();
			return false;
		}
		LOGI("Ad hoc group \"{}\" joined", name);
		return true;
	}

	void PspNetwork::AdhocLeaveGroup()
	{
		if (!_adhocActive) {
			return;
		}
		int state = AdhocctlStateDisconnected;
		if (sceNetAdhocctlGetState(&state) == 0 && state != AdhocctlStateDisconnected) {
			sceNetAdhocctlDisconnect();
			WaitForAdhocctlState(AdhocctlStateDisconnected, 5000);
		}
	}

	std::int32_t PspNetwork::AdhocScan(AdhocGroup* groups, std::int32_t maxCount)
	{
		if (!_adhocActive || groups == nullptr || maxCount <= 0) {
			return -1;
		}
		// A scan is refused while connected to a group, and its results are read back once the control
		// has returned to the disconnected state
		AdhocLeaveGroup();
		std::int32_t result = sceNetAdhocctlScan();
		if (result < 0) {
			LOGW("Cannot scan for ad hoc groups, sceNetAdhocctlScan() failed with error 0x{:.8x}", std::uint32_t(result));
			return -1;
		}
		// The scan is asynchronous, so the control may still report DISCONNECTED for a moment before it moves
		// to SCANNING - waiting for the end state alone would then return at once with nothing to read. The
		// first wait is what catches that, and it not happening at all is fine (a scan that finished already)
		WaitForAdhocctlState(AdhocctlStateScanning, 2000);
		if (!WaitForAdhocctlState(AdhocctlStateDisconnected, 15000)) {
			LOGW("Timed out scanning for ad hoc groups");
			return -1;
		}

		// The results are a linked list the firmware lays out in the buffer it is given; its size is queried first
		int length = 0;
		result = sceNetAdhocctlGetScanInfo(&length, nullptr);
		if (result < 0 || length <= 0) {
			return 0;
		}
		SceNetAdhocctlScanInfo buffer[32];
		if (length > (int)sizeof(buffer)) {
			length = (int)sizeof(buffer);
		}
		result = sceNetAdhocctlGetScanInfo(&length, buffer);
		if (result < 0) {
			return -1;
		}

		std::int32_t count = 0;
		for (const SceNetAdhocctlScanInfo* info = buffer; info != nullptr && count < maxCount; info = info->next) {
			AdhocGroup& group = groups[count++];
			std::memcpy(group.Name, info->name, 8);
			group.Name[8] = '\0';
			std::memcpy(group.Mac, info->bssid, 6);
		}
		return count;
	}

	void PspNetwork::MakeAdhocGroupName(Death::Containers::StringView source, char (&name)[9])
	{
		// The firmware allows 8 alphanumeric characters; everything else in a server name is skipped, and
		// a name with nothing usable in it becomes the game's own
		std::int32_t length = NormalizeAdhocName(source, name, std::int32_t(sizeof(name)) - 1);
		if (length == 0) {
			length = NormalizeAdhocName(NCINE_APP, name, std::int32_t(sizeof(name)) - 1);
		}
		name[length] = '\0';
	}

	bool PspNetwork::GetMacAddress(std::uint8_t (&mac)[6])
	{
		// The SDK header warns that the firmware asks for an 8-byte buffer even though it fills 6 of it
		std::uint8_t buffer[8] = {};
		if (sceWlanGetEtherAddr(buffer) < 0) {
			return false;
		}
		std::memcpy(mac, buffer, sizeof(mac));
		return true;
	}

	void PspNetwork::FormatMacAddress(const std::uint8_t (&mac)[6], char* buffer, std::size_t bufferLength)
	{
		if (bufferLength < (std::size_t)MacAddressStringLength + 1) {
			if (bufferLength > 0) {
				buffer[0] = '\0';
			}
			return;
		}
		static const char Digits[] = "0123456789ABCDEF";
		std::size_t pos = 0;
		for (std::int32_t i = 0; i < 6; i++) {
			buffer[pos++] = Digits[mac[i] >> 4];
			buffer[pos++] = Digits[mac[i] & 0x0F];
			if (i < 5) {
				buffer[pos++] = ':';
			}
		}
		buffer[pos] = '\0';
	}
}

#endif
