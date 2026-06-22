#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "NetworkManagerBase.h"
#include "MpGameMode.h"
#include "ServerInitialization.h"
#include "PeerDescriptor.h"
#include "../../nCine/Threading/LockedPtr.h"

namespace Jazz2::Actors::Multiplayer
{
	class MpPlayer;
}

namespace Jazz2::Multiplayer
{
	/**
		@brief Manages game-specific network connections
		
		Extends @ref NetworkManagerBase with the game session layer: holds the @ref ServerConfiguration, owns
		the per-peer @ref PeerDescriptor map (including retained descriptors for reconnecting players), drives
		LAN @ref ServerDiscovery, and overrides the connect/disconnect hooks to enforce server policy.

		@experimental
	*/
	class NetworkManager : public NetworkManagerBase
	{
	public:
		/** @brief Creates a new instance */
		NetworkManager();
		~NetworkManager();

		NetworkManager(const NetworkManager&) = delete;
		NetworkManager& operator=(const NetworkManager&) = delete;

		void CreateClient(INetworkHandler* handler, StringView endpoint, std::uint16_t defaultPort, std::uint32_t clientData) override;

		/** @brief Creates a server that accepts incoming connections */
		virtual bool CreateServer(INetworkHandler* handler, ServerConfiguration&& serverConfig);

		/**
		 * @brief Creates a socket-less local server for local splitscreen multiplayer
		 *
		 * Behaves like a server (authoritative game state, @ref NetworkState::Local) but binds no socket and accepts
		 * no remote connections. Local players are registered with @ref AddLocalPlayer().
		 */
		bool CreateLocalServer(INetworkHandler* handler, ServerConfiguration&& serverConfig);

		void Dispose() override;

		/** @brief Returns server configuration */
		ServerConfiguration& GetServerConfiguration() const;

		/** @brief Returns connected peer count */
		std::uint32_t GetPeerCount();

		/** @brief Returns all connected peers */
		LockedPtr<const HashMap<Peer, std::shared_ptr<PeerDescriptor>>, Spinlock> GetPeers();

		/** @brief Returns session peer descriptor for the local peer */
		std::shared_ptr<PeerDescriptor> GetPeerDescriptor(LocalPeerT);

		/**
		 * @brief Returns the session peer descriptor for the local splitscreen player at the given index
		 *
		 * Index zero is the default local peer (same as @ref GetPeerDescriptor(LocalPeerT)). Returns `nullptr` if no
		 * such local player has been registered with @ref AddLocalPlayer().
		 */
		std::shared_ptr<PeerDescriptor> GetPeerDescriptor(LocalPeerT, std::int32_t index);

		/**
		 * @brief Registers (or returns the existing) descriptor for a local splitscreen player
		 *
		 * The descriptor is inserted into the peer map under a synthetic local key (see @ref Peer::Local) so the
		 * shared game-mode logic iterates it like any other player, while its @ref PeerDescriptor::RemotePeer stays
		 * empty so it is treated as a local player. Index zero maps to the default local peer.
		 */
		std::shared_ptr<PeerDescriptor> AddLocalPlayer(std::int32_t index);

		/** @brief Returns session peer descriptor for the specified connected remote peer */
		std::shared_ptr<PeerDescriptor> GetPeerDescriptor(const Peer& peer);

		/**
		 * @brief Tries to reclaim a recently disconnected peer's descriptor by unique player ID
		 *
		 * Lets a reconnecting player resume. Returns the retained descriptor (with its restorable state), or `nullptr`
		 * if there is none within the reconnect window.
		 */
		std::shared_ptr<PeerDescriptor> ReclaimDisconnectedPeer(StaticArrayView<Uuid::Size, Uuid::Type> uuid);
		/** @brief Clears the restorable state of all retained disconnected peers (e.g., when a new round starts) */
		void ClearDisconnectedPeerStates();

		/** @brief Returns `true` if there are any inbound connections */
		bool HasInboundConnections() const;

		/** @brief Reloads server configuration from the source file */
		void RefreshServerConfiguration();

		/** @brief Sets status provider */
		void SetStatusProvider(std::weak_ptr<IServerStatusProvider> statusProvider);

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

		/** @brief Converts @ref MpGameMode to the string representation */
		static StringView GameModeToString(MpGameMode mode);
		/** @brief Converts the non-localized string representation back to @ref MpGameMode */
		static MpGameMode StringToGameMode(StringView value);
		/** @brief Converts UUID to the string representation */
		static String UuidToString(StaticArrayView<Uuid::Size, Uuid::Type> uuid);

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
		HashMap<String, std::shared_ptr<PeerDescriptor>> _disconnectedPeers; // Retained for reconnect, keyed by unique player ID
		Spinlock _lock;

		String OnOverrideContentPath(StringView path);

		static void FillServerConfigurationFromFile(StringView path, ServerConfiguration& serverConfig, HashMap<String, bool>& includedFiles, std::int32_t level);
		static void VerifyServerConfiguration(ServerConfiguration& serverConfig);
	};
}

#endif