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
		whichever thread is about to use the network, for only as long as it needs it (see
		@ref ScopedConnection).
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
			@brief Ends an access point association whose grace period has run out

			Called once a frame from the main loop, because @ref ScopedConnection deliberately does not drop
			the association the moment its last holder goes away - the next one is usually seconds off. Never
			waits for anything: when the association is busy being made on another thread it returns and the
			next frame tries again, so a frame is never held up by it.
		*/
		static void Update();

		/**
			@brief Holds an access point association for as long as it exists

			An association is what keeps the WLAN radio talking, and most of a session needs none - so it is
			reference counted rather than made once and left up: the first holder joins an access point and
			the last one to go away starts a grace period, after which @ref Update() drops the association.
			Everything that needs the network takes one of these for exactly as long as it does: the update
			check for its request, server discovery while the server list is open, the transport while a game
			is connected or hosted. The grace period is what makes the common sequence of those - leaving a
			game and opening the server list again - cost nothing instead of a second association.

			The association and the DHCP lease behind it are seconds of work, so a holder is taken on a thread
			that is not the main one, and the wait overlaps with whatever that thread is about to do. Holders
			are counted under a lock: one association is made and shared, so constructing one is cheap once
			the first exists, and concurrent constructions all wait for and receive the same answer.
		*/
		class ScopedConnection
		{
		public:
			/** @brief Joins an access point, unless @p acquire is `false`, in which case this holds nothing */
			explicit ScopedConnection(bool acquire = true);
			~ScopedConnection();

			ScopedConnection(const ScopedConnection&) = delete;
			ScopedConnection& operator=(const ScopedConnection&) = delete;

			/** @brief Whether an access point is joined */
			explicit operator bool() const {
				return _connected;
			}

		private:
			bool _acquired;
			bool _connected;
		};

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
