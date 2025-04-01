#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "NetworkManagerBase.h"
#include "MpGameMode.h"
#include "ServerInitialization.h"
#include "PeerDescriptor.h"

namespace Jazz2::Actors::Multiplayer
{
	class MpPlayer;
}

namespace Jazz2::Multiplayer
{
	/**
		@brief Manages game-specific network connections

		@experimental
	*/
	class NetworkManager : public NetworkManagerBase
	{
	public:
		NetworkManager();
		~NetworkManager();

		NetworkManager(const NetworkManager&) = delete;
		NetworkManager& operator=(const NetworkManager&) = delete;

		void CreateClient(INetworkHandler* handler, StringView endpoint, std::uint16_t defaultPort, std::uint32_t clientData) override;

		/** @brief Creates a server that accepts incoming connections */
		virtual bool CreateServer(INetworkHandler* handler, ServerConfiguration&& serverConfig);

		void Dispose() override;

		/** @brief Returns server configuration */
		ServerConfiguration& GetServerConfiguration() const;

		/** @brief Returns connected peer count */
		std::uint32_t GetPeerCount() const;

		/** @brief Returns all connected peers */
		const HashMap<Peer, std::shared_ptr<PeerDescriptor>>& GetPeers() const;

		/** @brief Returns session peer descriptor for the specified connected peer */
		std::shared_ptr<PeerDescriptor> GetPeerDescriptor(const Peer& peer);

		/** @brief Returns `true` if there are any inbound connections */
		bool HasInboundConnections() const;

		/**
		 * @brief Creates a default server configuration from the default template file
		 *
		 * This method reads a JSON configuration file described in @ref ServerConfiguration. If the JSON
		 * contains a @cpp "$include" @ce directive, it recursively loads the referenced files.
		 */
		static ServerConfiguration CreateDefaultServerConfiguration();
		
		/**
		 * @brief Loads a server configuration from the specified file
		 *
		 * This method reads a JSON configuration file described in @ref ServerConfiguration. If the JSON
		 * contains a @cpp "$include" @ce directive, it recursively loads the referenced files.
		 */
		static ServerConfiguration LoadServerConfigurationFromFile(StringView path);

		/** @brief Converts @ref MpGameMode to the localized string representation */
		static StringView GameModeToLocalizedString(MpGameMode mode);
		/** @brief Converts the non-localized string representation back to @ref MpGameMode */
		static MpGameMode StringToGameMode(StringView value);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		using NetworkManagerBase::CreateServer;
#endif

		ConnectionResult OnPeerConnected(const Peer& peer, std::uint32_t clientData) override;
		void OnPeerDisconnected(const Peer& peer, Reason reason) override;

	private:
		std::unique_ptr<ServerConfiguration> _serverConfig;
		std::unique_ptr<ServerDiscovery> _discovery;
		HashMap<Peer, std::shared_ptr<PeerDescriptor>> _peerDesc;

		static void FillServerConfigurationFromFile(StringView path, ServerConfiguration& serverConfig, HashMap<String, bool>& includedFiles, std::int32_t level);
	};
}

#endif