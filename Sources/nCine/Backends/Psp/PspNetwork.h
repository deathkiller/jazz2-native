#pragma once

#if defined(WITH_PSP) && defined(WITH_CURL)

#include <Containers/StringView.h>

namespace nCine::Backends
{
	/**
		@brief Socket stack of the PlayStation Portable, which has none of it running at boot

		The two network PRXes have to be loaded and the stack brought up before a single BSD call can work,
		and an access point has to be joined on top of that before anything can leave the console. The two
		halves are deliberately separate, because they cost very different amounts: @ref Initialize() is
		cheap and runs during startup, while joining an access point is seconds of work and is left to
		whichever thread is about to use the network (see @ref EnsureConnected()).
	*/
	class PspNetwork
	{
	public:
		PspNetwork() = delete;
		~PspNetwork() = delete;

		/** @brief Loads the network modules and brings the socket stack up, without joining anything */
		static void Initialize();

		/** @brief Tears the socket stack down again, if it ever came up */
		static void Shutdown();

		/**
			@brief Joins an access point and waits until it has handed out an address

			The association and the DHCP lease behind it are seconds of work, so this is not done during
			startup: every consumer of the network calls it first instead, on its own thread, and the wait
			overlaps with the application coming up rather than stalling in front of it. One association is
			made and shared, so this is cheap to call, safe to call from more than one thread, and returns
			the same answer to all of them.
		*/
		static bool EnsureConnected();

		/** @{ @name Ad hoc mode */

		/** @brief One ad hoc group found by @ref AdhocScan() */
		struct AdhocGroup
		{
			/** @brief Group name (up to 8 alphanumeric characters) */
			char Name[9];
			/** @brief MAC address of the console that created the group */
			std::uint8_t Mac[6];
		};

		/** @brief Length of a MAC address in its text form (`AA:BB:CC:DD:EE:FF`), without the terminator */
		static constexpr std::int32_t MacAddressStringLength = 17;

		/**
			@brief Switches the WLAN to ad hoc mode

			Ad hoc and infrastructure are exclusive on this console, down to their firmware modules, so the whole
			infrastructure half of the stack is torn down first - its module is unloaded and its memory given
			back, which the ad hoc module needs to load at all. Loads the ad hoc module and brings `sceNetAdhoc`
			and `sceNetAdhocctl` up; a no-op when already in ad hoc mode, and infrastructure mode is restored
			again by @ref AdhocEnd() or by any failure here.

			@returns `false` if the WLAN switch is off or the ad hoc stack cannot be brought up
		*/
		static bool AdhocBegin();
		/** @brief Leaves any group, tears the ad hoc stack down again and brings infrastructure mode back up */
		static void AdhocEnd();
		/** @brief Whether the WLAN is in ad hoc mode (see @ref AdhocBegin()) */
		static bool IsAdhocActive();
		/** @brief Creates an ad hoc group with the given name (see @ref MakeAdhocGroupName()) and waits until it is up */
		static bool AdhocCreateGroup(const char* name);
		/** @brief Joins the ad hoc group with the given name and waits until the console is connected to it */
		static bool AdhocJoinGroup(const char* name);
		/** @brief Leaves the current ad hoc group, if any */
		static void AdhocLeaveGroup();
		/**
			@brief Scans for ad hoc groups in reach and fills the given array with them

			Takes a few seconds, so it belongs on a thread that is not the main one. @returns the number of groups found, or `-1`
		*/
		static std::int32_t AdhocScan(AdhocGroup* groups, std::int32_t maxCount);
		/** @brief Derives a valid group name (up to 8 alphanumeric characters) from any text, such as a server name */
		static void MakeAdhocGroupName(Death::Containers::StringView source, char (&name)[9]);
		/** @brief Returns the console's own WLAN MAC address */
		static bool GetMacAddress(std::uint8_t (&mac)[6]);
		/** @brief Formats a MAC address as `AA:BB:CC:DD:EE:FF` (the buffer must hold @ref MacAddressStringLength + 1 characters) */
		static void FormatMacAddress(const std::uint8_t (&mac)[6], char* buffer, std::size_t bufferLength);

		/** @} */
	};
}

#endif
