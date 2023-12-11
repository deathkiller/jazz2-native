#pragma once

#if defined(WITH_MULTIPLAYER)

#include "Peer.h"
#include "Reason.h"
#include "ServerDiscovery.h"
#include "../../Common.h"
#include "../../nCine/Threading/Thread.h"
#include "../../nCine/Threading/ThreadSync.h"

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

struct _ENetHost;

using namespace Death::Containers;
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
		NetworkManager();
		~NetworkManager();

		bool CreateClient(INetworkHandler* handler, const StringView& address, std::uint16_t port, std::uint32_t clientData);
		bool CreateServer(INetworkHandler* handler, std::uint16_t port);
		void Dispose();

		NetworkState GetState() const;

		void SendToPeer(const Peer& peer, NetworkChannel channel, const std::uint8_t* data, std::size_t dataLength);
		void SendToAll(NetworkChannel channel, const std::uint8_t* data, std::size_t dataLength);
		void KickClient(const Peer& peer, Reason reason);

	private:
		NetworkManager(const NetworkManager&) = delete;
		NetworkManager& operator=(const NetworkManager&) = delete;

		static constexpr std::size_t MaxPeerCount = 64;
		static constexpr std::uint32_t ProcessingIntervalMs = 4;

		_ENetHost* _host;
		Thread _thread;
		NetworkState _state;
		SmallVector<_ENetPeer*, 1> _peers;
		INetworkHandler* _handler;
		Mutex _lock;
		std::unique_ptr<ServerDiscovery> _discovery;

		static std::int32_t _initializeCount;

		static void InitializeBackend();
		static void ReleaseBackend();

		static void OnClientThread(void* param);
		static void OnServerThread(void* param);
	};
}

#endif