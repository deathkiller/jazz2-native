#include "ServerDiscovery.h"

#if defined(WITH_MULTIPLAYER)

#include "NetworkManager.h"
#include "../../nCine/Base/Timer.h"

#include <Containers/String.h>
#include <IO/MemoryStream.h>

using namespace Death::IO;

namespace Jazz2::Multiplayer
{
	ServerDiscovery::ServerDiscovery(INetworkHandler* server, std::uint16_t port)
		: _server(server), _observer(nullptr), _actualPort(port)
	{
		DEATH_DEBUG_ASSERT(server != nullptr, , "server cannot be null");

		NetworkManager::InitializeBackend();

		_socket = TryCreateSocket("ff02::333", _address);
		if (_socket != ENET_SOCKET_NULL) {
			_thread.Run(ServerDiscovery::OnServerThread, this);
		}
	}

	ServerDiscovery::ServerDiscovery(IServerObserver* observer)
		: _server(nullptr), _observer(observer)
	{
		DEATH_DEBUG_ASSERT(observer != nullptr, , "observer cannot be null");

		NetworkManager::InitializeBackend();

		_socket = TryCreateSocket("ff02::333", _address);
		if (_socket != ENET_SOCKET_NULL) {
			_thread.Run(ServerDiscovery::OnClientThread, this);
		}
	}

	ServerDiscovery::~ServerDiscovery()
	{
		_server = nullptr;
		_observer = nullptr;
		_thread.Join();

		NetworkManager::ReleaseBackend();
	}

	ENetSocket ServerDiscovery::TryCreateSocket(const char* multicastAddress, ENetAddress& parsedAddress)
	{
		ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
		if (socket == ENET_SOCKET_NULL) {
			LOGE("Failed to create socket for server discovery");
			return ENET_SOCKET_NULL;
		}

		std::int32_t on = 1, ifidx = 0, hops = 3;
		if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on)) != 0 ||
			setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_IF, (const char*)&ifidx, sizeof(ifidx)) != 0 ||
			setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (const char*)&hops, sizeof(hops)) != 0 ||
			setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, (const char*)&on, sizeof(on)) != 0) {
			LOGE("Failed to enable multicast on socket for server discovery");
			enet_socket_destroy(socket);
			return ENET_SOCKET_NULL;
		}

		struct sockaddr_in6 saddr;
		std::memset(&saddr, 0, sizeof(saddr));
		saddr.sin6_family = AF_INET6;
		saddr.sin6_port = htons(DiscoveryPort);
		saddr.sin6_addr = in6addr_any;

		if (bind(socket, (struct sockaddr*)&saddr, sizeof(saddr))) {
			LOGE("Failed to bind socket for server discovery");
			enet_socket_destroy(socket);
			return ENET_SOCKET_NULL;
		}

		inet_pton(AF_INET6, multicastAddress, &parsedAddress.host);
		parsedAddress.port = DiscoveryPort;

		struct ipv6_mreq mreq;
		std::memcpy(&mreq.ipv6mr_multiaddr, &parsedAddress.host, sizeof(parsedAddress.host));
		mreq.ipv6mr_interface = ifidx;

		if (setsockopt(socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, (char*)&mreq, sizeof(mreq))) {
			LOGE("Failed to join multicast group on socket for server discovery");
			enet_socket_destroy(socket);
			return ENET_SOCKET_NULL;
		}

		return socket;
	}

	void ServerDiscovery::TrySendRequest(ENetSocket socket, const ENetAddress& address)
	{
		MemoryStream packet(9);
		packet.WriteValue<std::uint64_t>(PacketSignature);
		packet.WriteValue<std::uint8_t>(1);

		ENetBuffer sendbuf;
		sendbuf.data = (void*)packet.GetBuffer();
		sendbuf.dataLength = packet.GetSize();
		if (enet_socket_send(socket, &address, &sendbuf, 1) != (std::int32_t)sendbuf.dataLength) {
			LOGE("Failed to send discovery request");
		}
	}

	bool ServerDiscovery::ProcessResponses(ENetSocket socket, ServerDesc& discoveredServer, std::int32_t timeoutMs)
	{
		ENetSocketSet set;
		ENET_SOCKETSET_EMPTY(set);
		ENET_SOCKETSET_ADD(set, socket);
		if (enet_socketset_select(socket, &set, NULL, timeoutMs) <= 0) {
			return false;
		}

		std::uint8_t buffer[512];
		ENetBuffer recvbuf;
		recvbuf.data = buffer;
		recvbuf.dataLength = sizeof(buffer);
		const std::int32_t bytesRead = enet_socket_receive(socket, &discoveredServer.Endpoint, &recvbuf, 1);
		if (bytesRead <= 0) {
			return false;
		}

		char addressString[256];
		if (enet_address_get_host_ip(&discoveredServer.Endpoint, addressString, sizeof(addressString)) != 0) {
			return false;
		}

		discoveredServer.EndpointString = addressString;

		MemoryStream packet(buffer, bytesRead);
		std::uint64_t signature = packet.ReadValue<std::uint64_t>();
		std::uint8_t packetType = packet.ReadValue<std::uint8_t>();
		if (signature != PacketSignature || packetType != 2) {
			return false;
		}

		discoveredServer.Endpoint.port = packet.ReadValue<std::uint16_t>();
		packet.Read(discoveredServer.UniqueIdentifier, sizeof(discoveredServer.UniqueIdentifier));

		std::uint8_t nameLength = packet.ReadValue<std::uint8_t>();
		discoveredServer.Name = String(NoInit, nameLength);
		packet.Read(discoveredServer.Name.data(), nameLength);

		discoveredServer.GameModeAndFlags = packet.ReadVariableUint32();
		discoveredServer.CurrentPlayers = packet.ReadVariableUint32();
		discoveredServer.MaxPlayers = packet.ReadVariableUint32();

		nameLength = packet.ReadValue<std::uint8_t>();
		discoveredServer.LevelName = String(NoInit, nameLength);
		packet.Read(discoveredServer.LevelName.data(), nameLength);

		LOGI("Found server at [%s]:%u (%s)", addressString, discoveredServer.Endpoint.port, discoveredServer.LevelName.data());
		return true;
	}

	bool ServerDiscovery::ProcessRequests(ENetSocket socket, std::int32_t timeoutMs)
	{
		ENetSocketSet set;
		ENET_SOCKETSET_EMPTY(set);
		ENET_SOCKETSET_ADD(set, socket);
		if (enet_socketset_select(socket, &set, NULL, timeoutMs) <= 0) {
			return false;
		}

		ENetAddress endpoint;
		std::uint8_t buffer[512];
		ENetBuffer recvbuf;
		recvbuf.data = buffer;
		recvbuf.dataLength = sizeof(buffer);
		const std::int32_t bytesRead = enet_socket_receive(socket, &endpoint, &recvbuf, 1);
		if (bytesRead <= 0) {
			return false;
		}

		MemoryStream packet(buffer, bytesRead);
		std::uint64_t signature = packet.ReadValue<std::uint64_t>();
		std::uint8_t packetType = packet.ReadValue<std::uint8_t>();
		if (signature != PacketSignature || packetType != 1) {
			return false;
		}

		return true;
	}

	void ServerDiscovery::OnClientThread(void* param)
	{
		ServerDiscovery* _this = static_cast<ServerDiscovery*>(param);
		ENetSocket socket = _this->_socket;
		IServerObserver* observer = _this->_observer;

		while (_this->_observer != nullptr) {
			if (_this->_lastRequest.secondsSince() > 10) {
				_this->_lastRequest = TimeStamp::now();
				TrySendRequest(socket, _this->_address);
			}

			ServerDesc discoveredServer;
			if (ProcessResponses(socket, discoveredServer, 0)) {
				LOGW("TODO Process responses");
				observer->OnServerFound(std::move(discoveredServer));
			} else {
				// No responses, sleep for a while
				Timer::sleep(500);
			}
		}

		if (_this->_socket != ENET_SOCKET_NULL) {
			enet_socket_destroy(_this->_socket);
			_this->_socket = ENET_SOCKET_NULL;
		}

		LOGD("Server discovery thread exited");
	}

	void ServerDiscovery::OnServerThread(void* param)
	{
		ServerDiscovery* _this = static_cast<ServerDiscovery*>(param);
		ENetSocket socket = _this->_socket;
		INetworkHandler* server = _this->_server;
		std::int32_t delayCount = 0;

		while (_this->_server != nullptr) {
			delayCount--;
			if (delayCount <= 0) {
				delayCount = 10;

				while (_this->_address.port != 0 && ProcessRequests(socket, 0)) {
					if (_this->_lastRequest.secondsSince() > 15) {
						_this->_lastRequest = TimeStamp::now();

						LOGW("TODO Send responses");

						MemoryStream packet(512);
						packet.WriteValue<std::uint64_t>(PacketSignature);
						packet.WriteValue<std::uint8_t>(2);
						packet.WriteValue<std::uint16_t>(_this->_actualPort);

						//packet.Write(server->GetUniqueIdentifier(), UniqueIdentifierLength);
						static const std::uint8_t UniqueIdentifier[16] = { 1, 2, 3, 4, 5, 6 };
						packet.Write(UniqueIdentifier, UniqueIdentifierLength);

						// TODO: auto name = server->GetName();
						auto name = String("Test server");
						packet.WriteValue<std::uint8_t>((std::uint8_t)name.size());
						packet.Write(name.data(), (std::uint8_t)name.size());

						packet.WriteVariableUint32(0); // TODO: GameModeAndFlags
						//packet.WriteVariableUint32(server->GetCurrentPlayers());
						packet.WriteVariableUint32(7); // TODO: CurrentPlayers
						//packet.WriteVariableUint32(server->GetMaxPlayers());
						packet.WriteVariableUint32(32); // TODO: MaxPlayers

						// TODO: Current level
						auto levelName = String("unknown/unknown");
						packet.WriteValue<std::uint8_t>((std::uint8_t)levelName.size());
						packet.Write(levelName.data(), (std::uint8_t)levelName.size());

						ENetBuffer sendbuf;
						sendbuf.data = (void*)packet.GetBuffer();
						sendbuf.dataLength = packet.GetSize();
						if (enet_socket_send(socket, &_this->_address, &sendbuf, 1) != (std::int32_t)sendbuf.dataLength) {
							LOGE("Failed to send discovery response");
						}
					}
				}
			}

			Timer::sleep(500);
		}

		if (_this->_socket != ENET_SOCKET_NULL) {
			enet_socket_destroy(_this->_socket);
			_this->_socket = ENET_SOCKET_NULL;
		}

		LOGD("Server discovery thread exited");
	}
}

#endif