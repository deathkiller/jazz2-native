#include "PspNetwork.h"

#if defined(WITH_PSP) && defined(WITH_CURL)

#include "../../../Main.h"

#include <pspkernel.h>
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
	// Whether an access point is joined (see EnsureConnected()), and whether the WLAN is in ad hoc mode
	// instead - the two are exclusive, so switching to ad hoc drops the association and clears this
	static bool _apConnected = false;
	static bool _adhocActive = false;

	// The states sceNetAdhocctlGetState() reports; the SDK header names none of them
	static constexpr int AdhocctlStateDisconnected = 0;
	static constexpr int AdhocctlStateConnected = 1;
	static constexpr int AdhocctlStateScanning = 2;

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
		AdhocEnd();
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
		if (!_initialized || _adhocActive) {
			// In ad hoc mode there is no access point to join - the WLAN is busy being the network itself
			return false;
		}

		// There is one association at a time, and more than one thread can arrive here - the update check,
		// the server list and the multiplayer transport all do. A second caller while the first is still
		// associating has its sceNetApctlConnect() refused and then polls the same state machine to the same
		// answer, so no lock is needed for them to agree.
		if (!_apConnected) {
			_apConnected = DoEnsureConnected();
		}
		return _apConnected;
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

		// The WLAN is either associated with an access point or an ad hoc peer, never both
		if (_apConnected) {
			sceNetApctlDisconnect();
			_apConnected = false;
		}

		std::int32_t result = sceUtilityLoadNetModule(PSP_NET_MODULE_ADHOC);
		if (result < 0) {
			LOGW("Cannot load the ad hoc module, sceUtilityLoadNetModule() failed with error 0x{:.8x}", std::uint32_t(result));
			return false;
		}
		result = sceNetAdhocInit();
		if (result < 0) {
			LOGW("Cannot enter ad hoc mode, sceNetAdhocInit() failed with error 0x{:.8x}", std::uint32_t(result));
			sceUtilityUnloadNetModule(PSP_NET_MODULE_ADHOC);
			return false;
		}
		// The product code is what keeps groups of this game apart from other games' on the same channel
		struct productStruct product = {};
		product.unknown = 0;
		std::memcpy(product.product, "JAZZ20000", 9);
		result = sceNetAdhocctlInit(0x2000, 0x30, &product);
		if (result < 0) {
			LOGW("Cannot enter ad hoc mode, sceNetAdhocctlInit() failed with error 0x{:.8x}", std::uint32_t(result));
			sceNetAdhocTerm();
			sceUtilityUnloadNetModule(PSP_NET_MODULE_ADHOC);
			return false;
		}

		_adhocActive = true;
		LOGI("Ad hoc mode entered");
		return true;
	}

	void PspNetwork::AdhocEnd()
	{
		if (!_adhocActive) {
			return;
		}
		AdhocLeaveGroup();
		sceNetAdhocctlTerm();
		sceNetAdhocTerm();
		sceUtilityUnloadNetModule(PSP_NET_MODULE_ADHOC);
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
		if (!WaitForAdhocctlState(AdhocctlStateDisconnected, 15000)) {
			LOGW("Timed out scanning for ad hoc groups");
			return -1;
		}

		// The results are a linked list the firmware lays out in the buffer it is given; its size is queried first
		int length = 0;
		if (sceNetAdhocctlGetScanInfo(&length, nullptr) < 0 || length <= 0) {
			return 0;
		}
		SceNetAdhocctlScanInfo buffer[32];
		if (length > (int)sizeof(buffer)) {
			length = (int)sizeof(buffer);
		}
		if (sceNetAdhocctlGetScanInfo(&length, buffer) < 0) {
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
		std::int32_t length = 0;
		for (char c : source) {
			if (length >= 8) {
				break;
			}
			if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) {
				name[length++] = c;
			} else if (c >= 'a' && c <= 'z') {
				name[length++] = (char)(c - 'a' + 'A');
			}
		}
		if (length == 0) {
			std::memcpy(name, "JAZZ2", 5);
			length = 5;
		}
		name[length] = '\0';
	}

	bool PspNetwork::GetMacAddress(std::uint8_t (&mac)[6])
	{
		return (sceWlanGetEtherAddr(mac) >= 0);
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
