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

	enum class NetworkChannel : std::uint8_t
	{
		Main,
		UnreliableUpdates,
		Count
	};

	enum class NetworkState
	{
		None,
		Listening,
		Connecting,
		Connected
	};

	class NetworkManager
	{
		friend class ServerDiscovery;

	public:
		static constexpr std::size_t MaxPeerCount = 128;

		NetworkManager();
		~NetworkManager();

		NetworkManager(const NetworkManager&) = delete;
		NetworkManager& operator=(const NetworkManager&) = delete;

		bool CreateClient(INetworkHandler* handler, StringView address, std::uint16_t port, std::uint32_t clientData);
		bool CreateServer(INetworkHandler* handler, std::uint16_t port);
		void Dispose();

		NetworkState GetState() const;

		void SendToPeer(const Peer& peer, NetworkChannel channel, const std::uint8_t* data, std::size_t dataLength);
		void SendToPeer(const Peer& peer, NetworkChannel channel, const MemoryStream& packet);
		void SendToAll(NetworkChannel channel, const std::uint8_t* data, std::size_t dataLength);
		void SendToAll(NetworkChannel channel, const MemoryStream& packet);
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