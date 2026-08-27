#include "PspNetwork.h"

#if defined(WITH_PSP) && defined(WITH_CURL)

#include "../../../Main.h"

#include <pspkernel.h>
#include <psputility.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>
#include <pspnet_resolver.h>

namespace nCine::Backends
{
	// Set once the stack itself is up, so it is not torn down again when it never came up
	static bool _initialized = false;

	// None of the failures here are fatal to the application - all that is lost is the network - hence the
	// warnings rather than errors. The error codes are formatted unsigned, because they all have the high
	// bit set (0x8011....) and would print as the negative `int` the SDK returns them as.
	void PspNetwork::Initialize()
	{
		// The stack lives in these two firmware modules and neither one is loaded into a user-mode
		// application by default. COMMON has to go first, INET is what implements the BSD sockets.
		sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
		sceUtilityLoadNetModule(PSP_NET_MODULE_INET);

		// The pool is what every socket buffer is carved out of, so it also caps how much can be in flight;
		// 128 KB is what the firmware's own network applications ask for. The two priority/stack pairs
		// belong to the callout and interrupt threads the stack creates for itself.
		std::int32_t result = sceNetInit(128 * 1024, 42, 4 * 1024, 42, 4 * 1024);
		if (result < 0) {
			LOGW("Failed to initialize the network stack, sceNetInit() failed with error 0x{:.8x}", std::uint32_t(result));
			return;
		}
		result = sceNetInetInit();
		if (result < 0) {
			LOGW("Failed to initialize the network stack, sceNetInetInit() failed with error 0x{:.8x}", std::uint32_t(result));
			return;
		}
		// Host names are resolved through gethostbyname(), which the newlib port routes into this resolver -
		// without it every transfer fails with "Could not resolve host"
		result = sceNetResolverInit();
		if (result < 0) {
			LOGW("Failed to initialize the network stack, sceNetResolverInit() failed with error 0x{:.8x}", std::uint32_t(result));
			return;
		}
		result = sceNetApctlInit(0x8000, 48);
		if (result < 0) {
			LOGW("Failed to initialize the network stack, sceNetApctlInit() failed with error 0x{:.8x}", std::uint32_t(result));
			return;
		}

		_initialized = true;
	}

	void PspNetwork::Shutdown()
	{
		if (!_initialized) {
			return;
		}

		_initialized = false;
		sceNetApctlDisconnect();
		sceNetApctlTerm();
		sceNetResolverTerm();
		sceNetInetTerm();
		sceNetTerm();
		sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
		sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
	}

	static bool DoEnsureConnected()
	{
		// Index 1 is the first of the network configurations saved in the system settings (PPSSPP always
		// provides one, and needs "Enable networking/WLAN" turned on for it to be joinable)
		std::int32_t result = sceNetApctlConnect(1);
		if (result < 0) {
			LOGW("No network connection is available, sceNetApctlConnect() failed with error 0x{:.8x}", std::uint32_t(result));
			return false;
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

	bool PspNetwork::EnsureConnected()
	{
		if (!_initialized) {
			return false;
		}

		// There is one association for the lifetime of the application, and more than one thread can arrive
		// here - the update check, the server list and the multiplayer transport all do - so the first one
		// through does the waiting and the rest get its answer
		static const bool connected = DoEnsureConnected();
		return connected;
	}
}

#endif
