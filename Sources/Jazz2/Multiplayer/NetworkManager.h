#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "NetworkManagerBase.h"
#include "MpGameMode.h"
#include "ServerInitialization.h"
#include "../PlayerType.h"

namespace Jazz2::Multiplayer
{
	/**
		@brief Client configuration received from the server
	*/
	struct ClientConfiguration
	{
		/** @brief Multiplayer game mode */
		MpGameMode GameMode;
		/** @brief Welcome message in the lobby */
		String WelcomeMessage;
	};

	/**
		@brief Manages game-specific network connections

		@experimental
	*/
	class NetworkManager : public NetworkManagerBase
	{
	public:
		/** @brief Peer descriptor */
		struct PeerDesc {
			/** @brief Unique Player ID */
			StaticArray<16, std::uint8_t> Uuid;
			/** @brief Whether the peer is already successfully authenticated */
			bool IsAuthenticated;
			/** @brief Preferred player type selected by the peer */
			PlayerType PreferredPlayerType;
			/** @brief Player display name */
			String PlayerName;

			PeerDesc();
		};

		NetworkManager();
		~NetworkManager();

		NetworkManager(const NetworkManager&) = delete;
		NetworkManager& operator=(const NetworkManager&) = delete;

		void CreateClient(INetworkHandler* handler, StringView endpoint, std::uint16_t defaultPort, std::uint32_t clientData) override;

		/** @brief Creates a server that accepts incoming connections */
		virtual bool CreateServer(INetworkHandler* handler, ServerConfiguration&& serverConfig);

		void Dispose() override;

		/** @brief Returns client configuration */
		ClientConfiguration& GetClientConfiguration();

		/** @brief Returns server configuration */
		ServerConfiguration& GetServerConfiguration();

		/** @brief Returns connected peer count */
		std::uint32_t GetPeerCount() const;
		/** @brief Returns global (session) peer descriptor for the specified connected peer */
		PeerDesc* GetPeerDescriptor(const Peer& peer);

		/** @brief Creates a default server configuration from the template file */
		static ServerConfiguration CreateDefaultServerConfiguration();

	protected:
		using NetworkManagerBase::CreateServer;

		ConnectionResult OnPeerConnected(const Peer& peer, std::uint32_t clientData) override;
		void OnPeerDisconnected(const Peer& peer, Reason reason) override;

	private:
		std::unique_ptr<ClientConfiguration> _clientConfig;
		std::unique_ptr<ServerConfiguration> _serverConfig;
		std::unique_ptr<ServerDiscovery> _discovery;
		HashMap<Peer, PeerDesc> _peerDesc; // Server: Per peer descriptor
	};
}

#endif