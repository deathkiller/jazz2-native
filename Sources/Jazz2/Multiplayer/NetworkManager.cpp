#define ENET_IMPLEMENTATION
#include "NetworkManager.h"

#if defined(WITH_MULTIPLAYER)

#include "INetworkHandler.h"
#include "../../nCine/Base/Timer.h"

/*
// <mmeapi.h> included by "enet.h" still uses `far` macro
#define far

#define ENET_IMPLEMENTATION
#define ENET_FEATURE_ADDRESS_MAPPING
#if defined(DEATH_DEBUG)
#	define ENET_DEBUG
#endif
#include "Backends/enet.h"

// Undefine it again after include
#undef far
*/

#include <Containers/String.h>
#include <Threading/Interlocked.h>

using namespace Death;

namespace Jazz2::Multiplayer
{
	std::int32_t NetworkManager::_initializeCount = 0;

	NetworkManager::NetworkManager()
		: _host(nullptr), _state(NetworkState::None), _handler(nullptr)
	{
		InitializeBackend();
	}

	NetworkManager::~NetworkManager()
	{
		Dispose();
		ReleaseBackend();
	}

	bool NetworkManager::CreateClient(INetworkHandler* handler, const StringView& address, std::uint16_t port, std::uint32_t clientData)
	{
		if (_host != nullptr) {
			return false;
		}

		_host = enet_host_create(nullptr, 1, (std::size_t)NetworkChannel::Count, 0, 0);
		RETURNF_ASSERT_MSG(_host != nullptr, "Failed to create client");

		_state = NetworkState::Connecting;

		ENetAddress addr = { };
		enet_address_set_host(&addr, String::nullTerminatedView(address).data());
		addr.port = port;

		ENetPeer* peer = enet_host_connect(_host, &addr, (std::size_t)NetworkChannel::Count, clientData);
		if (peer == nullptr) {
			LOGE("Failed to create peer");
			_state = NetworkState::None;
			enet_host_destroy(_host);
			_host = nullptr;
			return false;
		}

		_peers.push_back(peer);

		_handler = handler;
		_thread.Run(NetworkManager::OnClientThread, this);
		return true;
	}

	bool NetworkManager::CreateServer(INetworkHandler* handler, std::uint16_t port)
	{
		if (_host != nullptr) {
			return false;
		}

		ENetAddress addr = { };
		addr.host = ENET_HOST_ANY;
		addr.port = port;

		_host = enet_host_create(&addr, MaxPeerCount, (std::size_t)NetworkChannel::Count, 0, 0);
		RETURNF_ASSERT_MSG(_host != nullptr, "Failed to create a server");

		_discovery = std::make_unique<ServerDiscovery>(handler, port);

		_handler = handler;
		_state = NetworkState::Listening;
		_thread.Run(NetworkManager::OnServerThread, this);
		return true;
	}

	void NetworkManager::Dispose()
	{
		if (_host == nullptr) {
			return;
		}

		_state = NetworkState::None;
		_thread.Join();

		_host = nullptr;
	}

	NetworkState NetworkManager::GetState() const
	{
		return _state;
	}

	void NetworkManager::SendToPeer(const Peer& peer, NetworkChannel channel, const std::uint8_t* data, std::size_t dataLength)
	{
		ENetPeer* target;
		if (peer == nullptr) {
			if (_state != NetworkState::Connected || _peers.empty()) {
				return;
			}

			target = _peers[0];
		} else {
			target = peer._enet;
		}

		enet_uint32 flags;
		if (channel == NetworkChannel::Main) {
			flags = ENET_PACKET_FLAG_RELIABLE;
		} else {
			flags = ENET_PACKET_FLAG_UNSEQUENCED;
		}

		ENetPacket* packet = enet_packet_create(data, dataLength, flags);

		_lock.Lock();
		if (enet_peer_send(target, (std::uint8_t)channel, packet) < 0) {
			enet_packet_destroy(packet);
		} else if (channel == NetworkChannel::UnreliableUpdates) {
			enet_host_flush(_host);
		}
		_lock.Unlock();
	}

	void NetworkManager::SendToAll(NetworkChannel channel, const std::uint8_t* data, std::size_t dataLength)
	{
		if (_peers.empty()) {
			return;
		}

		enet_uint32 flags;
		if (channel == NetworkChannel::Main) {
			flags = ENET_PACKET_FLAG_RELIABLE;
		} else {
			flags = ENET_PACKET_FLAG_UNSEQUENCED;
		}

		ENetPacket* packet = enet_packet_create(data, dataLength, flags);

		_lock.Lock();
		bool success = false;
		for (ENetPeer* peer : _peers) {
			if (enet_peer_send(peer, (std::uint8_t)channel, packet) >= 0) {
				success = true;
			}
		}
		if (!success) {
			enet_packet_destroy(packet);
		} else if (channel == NetworkChannel::UnreliableUpdates) {
			enet_host_flush(_host);
		}
		_lock.Unlock();
	}

	void NetworkManager::KickClient(const Peer& peer, Reason reason)
	{
		enet_peer_disconnect_now(peer._enet, (std::uint32_t)reason);
	}

	void NetworkManager::InitializeBackend()
	{
		if (Interlocked::Increment(&_initializeCount) == 1) {
			std::int32_t error = enet_initialize();
			RETURN_ASSERT_MSG(error == 0, "enet_initialize() failed with error %i", error);
		}
	}

	void NetworkManager::ReleaseBackend()
	{
		if (Interlocked::Decrement(&_initializeCount) == 0) {
			enet_deinitialize();
		}
	}

	void NetworkManager::OnClientThread(void* param)
	{
		NetworkManager* _this = static_cast<NetworkManager*>(param);
		INetworkHandler* handler = _this->_handler;
		ENetHost* host = _this->_host;

		ENetEvent ev;
		std::int32_t n = 10;
		while (n > 0) {
			if (_this->_state == NetworkState::None) {
				n = 0;
				break;
			}

			if (enet_host_service(host, &ev, 1000) >= 0 && ev.type == ENET_EVENT_TYPE_CONNECT) {
				break;
			}

			n--;
		}

		Reason reason;
		if (n <= 0) {
			LOGE("Failed to connect to the server");
			_this->_state = NetworkState::None;
			reason = Reason::ConnectionTimedOut;
		} else {
			_this->_state = NetworkState::Connected;
			handler->OnPeerConnected(ev.peer, ev.data);
			reason = Reason::Unknown;

			while (_this->_state != NetworkState::None) {
				_this->_lock.Lock();
				std::int32_t result = enet_host_service(host, &ev, 0);
				_this->_lock.Unlock();
				if (result <= 0) {
					if (result < 0) {
						LOGE("enet_host_service() returned %i", result);
						reason = Reason::ConnectionLost;
						break;
					}
					Timer::sleep(ProcessingIntervalMs);
					continue;
				}

				switch (ev.type) {
					case ENET_EVENT_TYPE_RECEIVE:
						handler->OnPacketReceived(ev.peer, ev.channelID, ev.packet->data, ev.packet->dataLength);
						enet_packet_destroy(ev.packet);
						break;
					case ENET_EVENT_TYPE_DISCONNECT:
						_this->_state = NetworkState::None;
						reason = (Reason)ev.data;
						break;
					case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
						_this->_state = NetworkState::None;
						reason = Reason::ConnectionLost;
						break;
				}
			}
		}

		handler->OnPeerDisconnected(_this->_peers[0], reason);

		for (ENetPeer* peer : _this->_peers) {
			enet_peer_disconnect_now(peer, (std::uint32_t)Reason::Disconnected);
		}
		_this->_peers.clear();

		enet_host_destroy(_this->_host);
		_this->_host = nullptr;
		_this->_handler = nullptr;

		_this->_thread.Detach();

		LOGD("Client thread exited (%u)", (std::uint32_t)reason);
	}

	void NetworkManager::OnServerThread(void* param)
	{
		NetworkManager* _this = static_cast<NetworkManager*>(param);
		INetworkHandler* handler = _this->_handler;
		ENetHost* host = _this->_host;

		ENetEvent ev;
		while (_this->_state != NetworkState::None) {
			_this->_lock.Lock();
			std::int32_t result = enet_host_service(host, &ev, 0);
			_this->_lock.Unlock();
			if (result <= 0) {
				if (result < 0) {
					LOGE("enet_host_service() returned %i", result);

					// Server failed, try to recreate it
					_this->_lock.Lock();

					for (auto& peer : _this->_peers) {
						handler->OnPeerDisconnected(peer, Reason::ConnectionLost);
					}
					_this->_peers.clear();

					ENetAddress addr = _this->_host->address;
					enet_host_destroy(_this->_host);
					host = enet_host_create(&addr, MaxPeerCount, (std::size_t)NetworkChannel::Count, 0, 0);
					_this->_host = host;

					_this->_lock.Unlock();

					if (host == nullptr) {
						LOGE("Failed to recreate the server");
						break;
					}
				}
				Timer::sleep(ProcessingIntervalMs);
				continue;
			}

			switch (ev.type) {
				case ENET_EVENT_TYPE_CONNECT: {
					ConnectionResult result = handler->OnPeerConnected(ev.peer, ev.data);
					if (result.IsSuccessful()) {
						_this->_peers.push_back(ev.peer);
					} else {
						enet_peer_disconnect_now(ev.peer, (std::uint32_t)result.FailureReason);
					}
					break;
				}
				case ENET_EVENT_TYPE_RECEIVE:
					handler->OnPacketReceived(ev.peer, ev.channelID, ev.packet->data, ev.packet->dataLength);
					enet_packet_destroy(ev.packet);
					break;
				case ENET_EVENT_TYPE_DISCONNECT:
					handler->OnPeerDisconnected(ev.peer, (Reason)ev.data);
					break;
				case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
					handler->OnPeerDisconnected(ev.peer, Reason::ConnectionLost);
					break;
			}
		}

		for (ENetPeer* peer : _this->_peers) {
			enet_peer_disconnect_now(peer, (std::uint32_t)Reason::ServerStopped);
		}
		_this->_peers.clear();

		enet_host_destroy(_this->_host);
		_this->_host = nullptr;
		_this->_handler = nullptr;

		_this->_thread.Detach();

		LOGD("Server thread exited");
	}
}

#endif