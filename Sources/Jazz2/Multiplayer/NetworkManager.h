#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Peer.h"
#include "Reason.h"
#include "ServerDiscovery.h"
#include "../../Main.h"
#include "../../nCine/Threading/Thread.h"
#include "../../nCine/Threading/ThreadSync.h"

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>
#include <IO/MemoryStream.h>

struct _ENetHost;

using namespace Death::Containers;
using namespace Death::IO;
using namespace nCine;

namespace Jazz2::Multiplayer
{
	class INetworkHandler;

	/** @brief Network packet channel */
	enum class NetworkChannel : std::uint8_t
	{
		Main,				/**< Main */
		UnreliableUpdates,	/**< Unreliable updates */
		Count				/**< Count of supported channels */
	};

	/** @brief State of network connection */
	enum class NetworkState
	{
		None,				/**< Disconnected */
		Listening,			/**< Listening */
		Connecting,			/**< Connecting to server */
		Connected			/**< Connected to server */
	};

	/**
		@brief Allows to create network clients and servers

		@experimental
	*/
	class NetworkManager
	{
		friend class ServerDiscovery;

	public:
		/** @brief Maximum connected peer count */
		static constexpr std::size_t MaxPeerCount = 128;

		NetworkManager();
		~NetworkManager();

		NetworkManager(const NetworkManager&) = delete;
		NetworkManager& operator=(const NetworkManager&) = delete;

		/** @brief Creates a client connection */
		bool CreateClient(INetworkHandler* handler, StringView address, std::uint16_t port, std::uint32_t clientData);
		/** @brief Creates a server */
		bool CreateServer(INetworkHandler* handler, std::uint16_t port);
		/** @brief Disposes all active connections */
		void Dispose();

		/** @brief Returns state of network connection */
		NetworkState GetState() const;

		/** @brief Sends a packet to a given peer */
		void SendToPeer(const Peer& peer, NetworkChannel channel, std::uint8_t packetType, ArrayView<const std::uint8_t> data);
		/** @brief Sends a packet to all connected peers */
		void SendToAll(NetworkChannel channel, std::uint8_t packetType, ArrayView<const std::uint8_t> data);
		/** @brief Kicks a given peer from the server */
		void KickClient(const Peer& peer, Reason reason);

	private:
		static constexpr std::uint32_t ProcessingIntervalMs = 4;

		_ENetHost* _host;
		Thread _thread;
		NetworkState _state;
		SmallVector<_ENetPeer*, 1> _peers;
		INetworkHandler* _handler;
		Mutex _lock;
		std::unique_ptr<ServerDiscovery> _discovery;

		static void InitializeBackend();
		static void ReleaseBackend();

		static void OnClientThread(void* param);
		static void OnServerThread(void* param);
	};
}

#endif