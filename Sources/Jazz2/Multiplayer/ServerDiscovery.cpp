#include "ServerDiscovery.h"

#if defined(WITH_MULTIPLAYER)

#include "NetworkManager.h"
#include "PacketTypes.h"
#include "../PreferencesCache.h"
#include "../../nCine/Base/Algorithms.h"
#include "../../nCine/Threading/Thread.h"

#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringStlView.h>
#include <Containers/StringUtils.h>
#include <IO/MemoryStream.h>

#if defined(DEATH_TARGET_ANDROID)
#	include <net/if.h>
#endif

#include "../../simdjson/simdjson.h"

using namespace Death::Containers::Literals;
using namespace Death::IO;

using namespace std::string_view_literals;
using namespace simdjson;

namespace Jazz2::Multiplayer
{
	ServerDiscovery::ServerDiscovery(NetworkManager* server)
		: _server(server), _observer(nullptr)
	{
		DEATH_DEBUG_ASSERT(server != nullptr, "server is null", );

		NetworkManagerBase::InitializeBackend();

		_socket = TryCreateSocket("ff1e::333", _address);
		if (_socket != ENET_SOCKET_NULL) {
			_thread.Run(ServerDiscovery::OnServerThread, this);
		}
	}

	ServerDiscovery::ServerDiscovery(IServerObserver* observer)
		: _server(nullptr), _observer(observer)
	{
		DEATH_DEBUG_ASSERT(observer != nullptr, "observer is null", );

		NetworkManagerBase::InitializeBackend();

		_socket = TryCreateSocket("ff1e::333", _address);
		if (_socket != ENET_SOCKET_NULL) {
			_thread.Run(ServerDiscovery::OnClientThread, this);
		}
	}

	ServerDiscovery::~ServerDiscovery()
	{
		_server = nullptr;
		_observer = nullptr;

		_thread.Join();

		NetworkManagerBase::ReleaseBackend();
	}

	ENetSocket ServerDiscovery::TryCreateSocket(const char* multicastAddress, ENetAddress& parsedAddress)
	{
		ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
		if (socket == ENET_SOCKET_NULL) {
			LOGE("[MP] Failed to create socket for server discovery");
			return ENET_SOCKET_NULL;
		}

#if defined(DEATH_TARGET_ANDROID)
		std::int32_t ifidx = if_nametoindex("wlan0");
#else
		std::int32_t ifidx = 0;
#endif

		std::int32_t on = 1, hops = 3;
		if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on)) != 0 ||
			setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_IF, (const char*)&ifidx, sizeof(ifidx)) != 0 ||
			setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (const char*)&hops, sizeof(hops)) != 0 ||
			setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, (const char*)&on, sizeof(on)) != 0) {
			LOGE("[MP] Failed to enable multicast on socket for server discovery");
			enet_socket_destroy(socket);
			return ENET_SOCKET_NULL;
		}

		struct sockaddr_in6 saddr;
		std::memset(&saddr, 0, sizeof(saddr));
		saddr.sin6_family = AF_INET6;
		saddr.sin6_port = htons(DiscoveryPort);
		saddr.sin6_addr = in6addr_any;

		if (bind(socket, (struct sockaddr*)&saddr, sizeof(saddr))) {
			LOGE("[MP] Failed to bind socket for server discovery");
			enet_socket_destroy(socket);
			return ENET_SOCKET_NULL;
		}

		inet_pton(AF_INET6, multicastAddress, &parsedAddress.host);
		parsedAddress.port = DiscoveryPort;

		struct ipv6_mreq mreq;
		std::memcpy(&mreq.ipv6mr_multiaddr, &parsedAddress.host, sizeof(parsedAddress.host));
		mreq.ipv6mr_interface = ifidx;

		if (setsockopt(socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, (char*)&mreq, sizeof(mreq))) {
			LOGE("[MP] Failed to join multicast group on socket for server discovery");
			enet_socket_destroy(socket);
			return ENET_SOCKET_NULL;
		}

		return socket;
	}

	void ServerDiscovery::TrySendLocalRequest(ENetSocket socket, const ENetAddress& address)
	{
		MemoryStream packet(9);
		packet.WriteValue<std::uint64_t>(PacketSignature);
		packet.WriteValue<std::uint8_t>((std::uint8_t)BroadcastPacketType::DiscoveryRequest);

		ENetBuffer sendbuf;
		sendbuf.data = (void*)packet.GetBuffer();
		sendbuf.dataLength = packet.GetSize();
		if (enet_socket_send(socket, &address, &sendbuf, 1) != (std::int32_t)sendbuf.dataLength) {
			LOGE("[MP] Failed to send discovery request");
		}
	}

	void ServerDiscovery::TrySendOnlineRequest()
	{
		if (_onlineRequest.IsValid()) {
			return;
		}

		String url = "https://deat.tk/jazz2/servers?fetch&v=2&d="_s + PreferencesCache::GetDeviceID();
		_onlineRequest = WebSession::GetDefault().CreateRequest(url);
		_onlineRequest.SetHeader("User-Agent"_s, "Jazz2 Resurrection"_s);
		auto result = _onlineRequest.Execute();
		if (result) {
			auto s = _onlineRequest.GetResponse().GetStream();
			auto size = s->GetSize();
			auto buffer = std::make_unique<char[]>(size + simdjson::SIMDJSON_PADDING);
			s->Read(buffer.get(), size);
			buffer[size] = '\0';

			ondemand::parser parser;
			ondemand::document doc;
			if (parser.iterate(buffer.get(), size, size + simdjson::SIMDJSON_PADDING).get(doc) == SUCCESS) {
				ondemand::array servers;
				if (doc["s"].get(servers) == SUCCESS) {
					for (auto serverItem : servers) {
						std::string_view serverName, serverUuid, serverEndpoints;
						if (serverItem["n"].get(serverName) == SUCCESS && !serverName.empty() &&
							serverItem["u"].get(serverUuid) == SUCCESS && !serverUuid.empty() &&
							serverItem["e"].get(serverEndpoints) == SUCCESS && !serverEndpoints.empty()) {

							ServerDescription discoveredServer {};
							discoveredServer.Name = serverName;
							discoveredServer.EndpointString = serverEndpoints;
							discoveredServer.Name = serverName;

							LOGD("[MP] Found server \"%s\" at %s", discoveredServer.Name.data(), discoveredServer.EndpointString.data());
							_observer->OnServerFound(std::move(discoveredServer));
						}
					}
				}
			} else {
				LOGE("[MP] Failed to parse public server list");
			}
		} else {
			LOGE("[MP] Failed to download public server list: %s", result.error.data());
		}
		_onlineRequest = {};
	}

	bool ServerDiscovery::ProcessLocalResponses(ENetSocket socket, ServerDescription& discoveredServer, std::int32_t timeoutMs)
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
		BroadcastPacketType packetType = (BroadcastPacketType)packet.ReadValue<std::uint8_t>();
		if (signature != PacketSignature || packetType != BroadcastPacketType::DiscoveryResponse) {
			return false;
		}

		std::uint16_t port = packet.ReadValue<std::uint16_t>();

		discoveredServer.EndpointString = NetworkManagerBase::AddressToString(endpoint.host, port);
		if (discoveredServer.EndpointString.empty()) {
			return false;
		}

		packet.Read(discoveredServer.UniqueIdentifier, sizeof(discoveredServer.UniqueIdentifier));

		std::uint8_t nameLength = packet.ReadValue<std::uint8_t>();
		discoveredServer.Name = String(NoInit, nameLength);
		packet.Read(discoveredServer.Name.data(), nameLength);

		discoveredServer.Flags = packet.ReadVariableUint32();
		discoveredServer.GameMode = (MpGameMode)packet.ReadValue<std::uint8_t>();
		discoveredServer.CurrentPlayerCount = packet.ReadVariableUint32();
		discoveredServer.MaxPlayerCount = packet.ReadVariableUint32();

		nameLength = packet.ReadValue<std::uint8_t>();
		discoveredServer.LevelName = String(NoInit, nameLength);
		packet.Read(discoveredServer.LevelName.data(), nameLength);

		LOGD("[MP] Found server \"%s\" at %s", discoveredServer.Name.data(), discoveredServer.EndpointString.data());
		return true;
	}

	bool ServerDiscovery::ProcessLocalRequests(ENetSocket socket, std::int32_t timeoutMs)
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
		BroadcastPacketType packetType = (BroadcastPacketType)packet.ReadValue<std::uint8_t>();
		if (signature != PacketSignature || packetType != BroadcastPacketType::DiscoveryRequest) {
			return false;
		}

		return true;
	}

	void ServerDiscovery::PublishOnline()
	{
		if (_onlineRequest.IsValid()) {
			return;
		}

		auto& serverConfig = _server->GetServerConfiguration();
		if (serverConfig.ServerName.empty()) {
			return;
		}

		String serverName = StringUtils::replaceAll(StringUtils::replaceAll(serverConfig.ServerName.data(),
			"\\"_s, "\\\\"_s), "\""_s, "\\\""_s);

		char input[2048];
		std::int32_t length = formatString(input, sizeof(input), "{\"n\":\"%s\",\"u\":\"",
			serverName.data(), PreferencesCache::UniquePlayerID.data());

		auto& id = PreferencesCache::UniquePlayerID;
		length += formatString(input + length, sizeof(input) - length, "%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X",
			id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7], id[8], id[9], id[10], id[11], id[12], id[13], id[14], id[15]);

		length += formatString(input + length, sizeof(input) - length, "\",\"e\":\"");

		bool isFirst = true;
		auto endpoints = _server->GetServerEndpoints();
		for (auto& endpoint : endpoints) {
			if (isFirst) {
				isFirst = false;
			} else {
				length += formatString(input + length, sizeof(input) - length, "|");
			}

			length += formatString(input + length, sizeof(input) - length, "%s", endpoint.data());
		}

		length += formatString(input + length, sizeof(input) - length, "\",\"v\":\"%s\",\"d\":\"%s\",\"p\":%u,\"m\":%u}",
			NCINE_VERSION, PreferencesCache::GetDeviceID().data(), _server->GetPeerCount(), serverConfig.MaxPlayerCount);

		_onlineRequest = WebSession::GetDefault().CreateRequest("https://deat.tk/jazz2/servers");
		_onlineRequest.SetHeader("User-Agent"_s, "Jazz2 Resurrection"_s);
		_onlineRequest.SetMethod("POST"_s);
		_onlineRequest.SetData(StringView(input, length), "application/json"_s);
		auto result = _onlineRequest.Execute();
		if (!result) {
			LOGW("[MP] Failed to publish the server: %s", result.error.data());
		}
		_onlineRequest = {};
	}

	void ServerDiscovery::OnClientThread(void* param)
	{
		ServerDiscovery* _this = static_cast<ServerDiscovery*>(param);
		ENetSocket socket = _this->_socket;

		while (_this->_observer != nullptr) {
			if (_this->_lastLocalRequest.secondsSince() > 10) {
				_this->_lastLocalRequest = TimeStamp::now();
				_this->TrySendLocalRequest(socket, _this->_address);
			}

			if (_this->_lastOnlineRequest.secondsSince() > 60) {
				_this->_lastOnlineRequest = TimeStamp::now();
				_this->TrySendOnlineRequest();
			}

			ServerDescription discoveredServer;
			if (_this->ProcessLocalResponses(socket, discoveredServer, 0)) {
				_this->_observer->OnServerFound(std::move(discoveredServer));
			} else {
				// No responses, sleep for a while
				Thread::Sleep(500);
			}
		}

		if (_this->_socket != ENET_SOCKET_NULL) {
			enet_socket_destroy(_this->_socket);
			_this->_socket = ENET_SOCKET_NULL;
		}

		LOGD("[MP] Server discovery thread exited");
	}

	void ServerDiscovery::OnServerThread(void* param)
	{
		ServerDiscovery* _this = static_cast<ServerDiscovery*>(param);
		ENetSocket socket = _this->_socket;
		std::int32_t delayCount = 0;

		while (_this->_server != nullptr) {
			delayCount--;
			if (delayCount <= 0) {
				delayCount = 10;

				while (_this->_address.port != 0 && _this->ProcessLocalRequests(socket, 0)) {
					if (_this->_lastLocalRequest.secondsSince() > 15) {
						_this->_lastLocalRequest = TimeStamp::now();

						// If server name is empty, it's private and shouldn't respond to discovery messages
						auto& serverConfig = _this->_server->GetServerConfiguration();
						if (!serverConfig.ServerName.empty()) {
							MemoryStream packet(512);
							packet.WriteValue<std::uint64_t>(PacketSignature);
							packet.WriteValue<std::uint8_t>((std::uint8_t)BroadcastPacketType::DiscoveryResponse);
							packet.WriteValue<std::uint16_t>(serverConfig.ServerPort);
							packet.Write(PreferencesCache::UniquePlayerID.data(), PreferencesCache::UniquePlayerID.size());

							packet.WriteValue<std::uint8_t>((std::uint8_t)serverConfig.ServerName.size());
							packet.Write(serverConfig.ServerName.data(), (std::uint8_t)serverConfig.ServerName.size());

							std::uint32_t flags = 0;
							if (!serverConfig.ServerPassword.empty()) {
								flags |= 0x01;
							}
							if (!serverConfig.WhitelistedUniquePlayerIDs.empty()) {
								flags |= 0x02;
							}
							packet.WriteVariableUint32(flags);
							packet.WriteValue<std::uint8_t>((std::uint8_t)serverConfig.GameMode);

							packet.WriteVariableUint32(_this->_server->GetPeerCount());
							packet.WriteVariableUint32(serverConfig.MaxPlayerCount);

							// TODO: Current level
							auto levelName = String("unknown/unknown");
							packet.WriteValue<std::uint8_t>((std::uint8_t)levelName.size());
							packet.Write(levelName.data(), (std::uint8_t)levelName.size());

							ENetBuffer sendbuf;
							sendbuf.data = (void*)packet.GetBuffer();
							sendbuf.dataLength = packet.GetSize();
							if (enet_socket_send(socket, &_this->_address, &sendbuf, 1) != (std::int32_t)sendbuf.dataLength) {
								LOGE("[MP] Failed to send discovery response");
							}
						}
					}
				}

				if (_this->_lastOnlineRequest.secondsSince() > 600) {
					_this->_lastOnlineRequest = TimeStamp::now();
					_this->PublishOnline();
				}
			}

			Thread::Sleep(500);
		}

		if (_this->_socket != ENET_SOCKET_NULL) {
			enet_socket_destroy(_this->_socket);
			_this->_socket = ENET_SOCKET_NULL;
		}

		LOGD("[MP] Server discovery thread exited");
	}
}

#endif