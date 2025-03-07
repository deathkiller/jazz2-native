#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "NetworkManagerBase.h"
#include "MpGameMode.h"
#include "../PlayerType.h"

#include "../../nCine/Base/HashMap.h"

namespace Jazz2::Multiplayer
{
	/**
		@brief Manages game-specific network connections

		@experimental
	*/
	class NetworkManager : public NetworkManagerBase
	{
	public:
		/** @brief Peer descriptor */
		struct PeerDesc {
			/** @brief The first part of the unique Identifier (128-bit) */
			std::uint64_t Uuid1;
			/** @brief The second part of the unique Identifier (128-bit) */
			std::uint64_t Uuid2;
			/** @brief Whether the peer is already successfully authenticated */
			bool IsAuthenticated;
			/** @brief Preferred player type selected by the peer */
			PlayerType PreferredPlayerType;
			/** @brief Player display name */
			String PlayerName;

			PeerDesc();
		};

		// Server Configuration
		/** @brief Current multiplayer game mode */
		MpGameMode GameMode;
		/** @brief Server name */
		String ServerName;
		/** @brief Password of the server */
		String ServerPassword;
		/** @brief Welcome message in the lobby */
		String WelcomeMessage;
		/** @brief Maximum number of players */
		std::uint32_t MaxPlayerCount;
		/** @brief Allowed player types as bitmask of @ref PlayerType */
		std::uint8_t AllowedPlayerTypes;
		/** @brief Time after which inactive players will be kicked, in seconds, `-1` to disable */
		std::int32_t IdleKickTimeSecs;
		/** @brief List of whitelisted unique player IDs, value can contain user-defined comment */
		HashMap<String, String> WhitelistedUniquePlayerIDs;
		/** @brief List of banned unique player IDs, value can contain user-defined reason */
		HashMap<String, String> BannedUniquePlayerIDs;
		/** @brief List of banned IP addresses, value can contain user-defined reason */
		HashMap<String, String> BannedIPAddresses;

		NetworkManager();
		~NetworkManager();

		NetworkManager(const NetworkManager&) = delete;
		NetworkManager& operator=(const NetworkManager&) = delete;

		/** @brief Returns global (session) peer descriptor for the specified connected peer */
		PeerDesc* GetPeerDescriptor(const Peer& peer);

	protected:
		ConnectionResult OnPeerConnected(const Peer& peer, std::uint32_t clientData) override;
		void OnPeerDisconnected(const Peer& peer, Reason reason) override;

	private:
		HashMap<Peer, PeerDesc> _peerDesc; // Server: Per peer descriptor
	};
}

#endif