#define ENET_IMPLEMENTATION
#include "NetworkManagerBase.h"

#if defined(WITH_MULTIPLAYER)

#include "INetworkHandler.h"
#include "../../nCine/Base/Algorithms.h"
#include "../../nCine/Threading/Thread.h"

#include <atomic>
#include <mutex>

#include <Containers/GrowableArray.h>
#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringStl.h>
#include <Containers/StringStlView.h>

#if defined(DEATH_TARGET_ANDROID)
#	include "Backends/ifaddrs-android.h"
#elif defined(DEATH_TARGET_SWITCH)
#	include <net/if.h>
#elif defined(DEATH_TARGET_WINDOWS)
#	include <iphlpapi.h>
#elif !defined(DEATH_TARGET_EMSCRIPTEN)
#	include <ifaddrs.h>
#endif

#if defined(WITH_WEBSOCKET) && !defined(DEATH_TARGET_EMSCRIPTEN)
#	include <ixwebsocket/IXNetSystem.h>
#endif

using namespace Death;
using namespace Death::Containers::Literals;

namespace Jazz2::Multiplayer
{
	static std::atomic_int32_t _initializeCount{0};

#if defined(WITH_WEBSOCKET)
	/** @brief Appends `?clientData=N` (or `&clientData=N`) query parameter to a WebSocket URL */
	static String AppendClientDataToUrl(StringView url, std::uint32_t clientData)
	{
		bool hasQuery = (url.findOr('?', url.end()).begin() != url.end());
		char buffer[12];
		std::size_t length = formatInto(buffer, "{}", clientData);
		return url + (hasQuery ? "&clientData="_s : "?clientData="_s) + StringView(buffer, length);
	}

	/** @brief Parses a uint32_t query parameter from a URI string (e.g. /?clientData=123) */
	static std::uint32_t ParseQueryParamUInt32(StringView uri, StringView paramName)
	{
		auto parts = uri.partition('?');
		if (parts[1].empty()) {
			return 0;
		}

		StringView query = parts[2];
		while (!query.empty()) {
			auto p = query.partition('&');
			StringView pair = p[0];
			query = p[2];

			if (pair.hasPrefix(paramName) && pair.size() > paramName.size() && pair[paramName.size()] == '=') {
				StringView valueStr = pair.exceptPrefix(paramName.size() + 1);
				std::uint32_t result = 0;
				for (char c : valueStr) {
					if (c < '0' || c > '9') break;
					result = result * 10 + (c - '0');
				}
				return result;
			}
		}
		return 0;
	}

	/** @brief Extracts only the host[:port] from a WebSocket URL, stripping protocol prefix, path, and query string */
	static StringView ExtractHostFromWsUrl(StringView url)
	{
		if (url.hasPrefix("wss://"_s)) {
			url = url.exceptPrefix(6);
		} else if (url.hasPrefix("ws://"_s)) {
			url = url.exceptPrefix(5);
		}
		auto end = url.findAnyOr("/?"_s, url.end()).begin();
		return url.prefix(end);
	}

	/** @brief Converts a disconnect Reason to the corresponding WebSocket close code */
	static std::uint16_t ReasonToWsCloseCode(Reason reason)
	{
		switch (reason) {
			case Reason::Disconnected: return 1000;
			case Reason::ServerStopped:
			case Reason::ServerStoppedForMaintenance:
			case Reason::ServerStoppedForReconfiguration:
			case Reason::ServerStoppedForUpdate: return 1001;
			case Reason::Kicked: return 1008;
			default: return std::uint16_t(4000) + std::uint16_t(reason);
		}
	}

	/** @brief Converts a WebSocket close code back to a disconnect Reason */
	static Reason WsCloseCodeToReason(std::uint16_t code, bool wasConnected)
	{
		switch (code) {
			case 1000: return Reason::Disconnected;
			case 1001: return Reason::ServerStopped;
			case 1008: return Reason::Kicked;
			default:
				if (code >= 4000 && code < std::uint16_t(4000 + std::uint32_t(Reason::Idle) + 1)) {
					return Reason(code - 4000);
				}
				return wasConnected ? Reason::ConnectionLost : Reason::ConnectionTimedOut;
		}
	}
#endif

	NetworkManagerBase::NetworkManagerBase()
		:
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		_host(nullptr),
#endif
		_state(NetworkState::None), _handler(nullptr)
	{
		InitializeBackend();
	}

	NetworkManagerBase::~NetworkManagerBase()
	{
		Dispose();
		ReleaseBackend();
	}

	void NetworkManagerBase::CreateClient(INetworkHandler* handler, StringView endpoints, std::uint16_t defaultPort, std::uint32_t clientData)
	{
		if (_handler != nullptr) {
			LOGE("[MP] Client already created");
			return;
		}

		_state = NetworkState::Connecting;
		_clientData = clientData;
		_handler = handler;

#if defined(DEATH_TARGET_EMSCRIPTEN) && defined(WITH_WEBSOCKET)
		StringView firstEndpoint = endpoints.prefix(endpoints.findOr('|', endpoints.end()).begin());
		String wsUrl = AppendClientDataToUrl(firstEndpoint, clientData);

		EmscriptenWebSocketCreateAttributes attrs;
		emscripten_websocket_init_create_attributes(&attrs);
		attrs.url = wsUrl.data();
		attrs.createOnMainThread = EM_TRUE;

		_emWsSocket = emscripten_websocket_new(&attrs);
		if (_emWsSocket <= 0) {
			LOGE("[MP] Failed to create WebSocket connection to \"{}\"", firstEndpoint);
			_state = NetworkState::None;
			_handler = nullptr;
			return;
		}

		_emWsUrl = firstEndpoint;
		LOGI("[MP] Connecting to \"{}\"", _emWsUrl);

		emscripten_websocket_set_onopen_callback(_emWsSocket, this, OnEmWsOpen);
		emscripten_websocket_set_onmessage_callback(_emWsSocket, this, OnEmWsMessage);
		emscripten_websocket_set_onerror_callback(_emWsSocket, this, OnEmWsError);
		emscripten_websocket_set_onclose_callback(_emWsSocket, this, OnEmWsClose);
#else
#	if defined(WITH_WEBSOCKET)
		// If the first endpoint looks like a WebSocket URL, use IXWebSocket client
		StringView firstEndpoint = endpoints.prefix(endpoints.findOr('|', endpoints.end()).begin());
		if (firstEndpoint.hasPrefix("ws://"_s) || firstEndpoint.hasPrefix("wss://"_s)) {
			String wsUrl = AppendClientDataToUrl(firstEndpoint, clientData);
			LOGI("[MP] Connecting to \"{}\" via WebSocket transport", wsUrl);

			_wsClient = std::make_unique<ix::WebSocket>();
			_wsClient->setUrl(std::string(wsUrl.data(), wsUrl.size()));
			_wsClient->disableAutomaticReconnection();
			_wsClient->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
				if (msg->type == ix::WebSocketMessageType::Open) {
					std::unique_lock<Spinlock> lock(_wsLock);
					_wsPeers.emplace(std::uint64_t(1), WsPeerInfo{_wsClient.get(), {}});
					_wsPendingEvents.push_back({WsQueuedEvent::Type::Open, 1, {}, _clientData, 0});
				} else if (msg->type == ix::WebSocketMessageType::Close) {
					std::unique_lock<Spinlock> lock(_wsLock);
					_wsPeers.erase(std::uint64_t(1));
					_wsPendingEvents.push_back({WsQueuedEvent::Type::Close, 1, {}, 0, msg->closeInfo.code});
				} else if (msg->type == ix::WebSocketMessageType::Message && msg->binary) {
					if (!msg->str.empty()) {
						std::unique_lock<Spinlock> lock(_wsLock);
						_wsPendingEvents.push_back({WsQueuedEvent::Type::Message, 1, msg->str, 0, 0});
					}
				} else if (msg->type == ix::WebSocketMessageType::Error) {
					LOGE("[MP] WebSocket transport error: {}", StringView{msg->errorInfo.reason});
				}
			});
			_wsClient->start();
			_thread = Thread(NetworkManagerBase::OnClientWsThread, this);
			return;
		}
#	endif

		_desiredEndpoints.clear();

#	if defined(DEATH_TARGET_ANDROID)
		std::int32_t ifidx = 0;
		struct ifaddrs* ifaddr;
		struct ifaddrs* ifa;
		if (getifaddrs(&ifaddr) == 0) {
			for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
				if (ifa->ifa_addr != nullptr && (ifa->ifa_addr->sa_family == AF_INET || ifa->ifa_addr->sa_family == AF_INET6) &&
					(ifa->ifa_flags & IFF_UP)) {
					ifidx = if_nametoindex(ifa->ifa_name);
					if (ifidx > 0) {
						LOGI("[MP] Using {} interface \"{}\" ({})", ifa->ifa_addr->sa_family == AF_INET6
							? "IPv6" : "IPv4", ifa->ifa_name, ifidx);
						break;
					}
				}
			}
			freeifaddrs(ifaddr);
		}
		if (ifidx == 0) {
			LOGI("[MP] No suitable interface found");
			ifidx = if_nametoindex("wlan0");
		}
#	else
		std::int32_t ifidx = 0;
#	endif

		while (endpoints) {
			auto p = endpoints.partition('|');
			if (p[0]) {
				StringView address; std::uint16_t port;
				if (TrySplitAddressAndPort(p[0], address, port)) {
					ENetAddress addr = {};
					String nullTerminatedAddress = String::nullTerminatedView(address);
					std::int32_t r = enet_address_set_host(&addr, nullTerminatedAddress.data());
					//std::int32_t r = enet_address_set_host_ip(&addr, nullTerminatedAddress.data());
					if (r == 0) {
#	if ENET_IPV6
						if (addr.sin6_scope_id == 0) {
							addr.sin6_scope_id = (std::uint16_t)ifidx;
						}
#	endif
						addr.port = (port != 0 ? port : defaultPort);
						_desiredEndpoints.push_back(std::move(addr));
					} else {
#	if defined(DEATH_TARGET_WINDOWS)
						std::int32_t error = ::WSAGetLastError();
#	else
						std::int32_t error = errno;
#	endif
						LOGW("[MP] Failed to parse specified address \"{}\" with error {}", nullTerminatedAddress, error);
					}
				} else {
					LOGW("[MP] Failed to parse specified endpoint \"{}\"", p[0]);
				}
			}

			endpoints = p[2];
		}

		_thread = Thread(NetworkManagerBase::OnClientThread, this);
#endif
	}

	bool NetworkManagerBase::CreateServer(INetworkHandler* handler, std::uint16_t port)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		// Creating a server is not supported on Emscripten
		return false;
#else
		if (_handler != nullptr) {
			return false;
		}

		ENetAddress addr = {};
		addr.host = ENET_HOST_ANY;
		addr.port = port;

		_host = enet_host_create(&addr, MaxPeerCount, (std::size_t)NetworkChannel::Count, 0, 0);
		DEATH_ASSERT(_host != nullptr, "Failed to create a server", false);

		_handler = handler;
		_state = NetworkState::Listening;
		_thread = Thread(NetworkManagerBase::OnServerThread, this);
		return true;
#endif
	}

	void NetworkManagerBase::Dispose()
	{
#if defined(DEATH_TARGET_EMSCRIPTEN) && defined(WITH_WEBSOCKET)
		if (_emWsSocket <= 0) {
			return;
		}
		EMSCRIPTEN_WEBSOCKET_T socket = _emWsSocket;
		bool wasConnected = (_state == NetworkState::Connected);
		_emWsSocket = 0;
		_state = NetworkState::None;
		// Delete before calling OnPeerDisconnected to prevent any callbacks firing
		emscripten_websocket_close(socket, 1000, "Client disconnecting");
		emscripten_websocket_delete(socket);
		if (wasConnected) {
			OnPeerDisconnected(Peer::FromWebSocket(1), Reason::Disconnected);
		}
#else
		if (_host == nullptr
#	if defined(WITH_WEBSOCKET)
			&& _wsClient == nullptr
#	endif
		) {
			return;
		}

		_state = NetworkState::None;
		_thread.Join();

		_host = nullptr;
#endif

		_handler = nullptr;
	}

	NetworkState NetworkManagerBase::GetState() const
	{
		return _state;
	}

	std::uint32_t NetworkManagerBase::GetRoundTripTimeMs() const
	{
#if defined(DEATH_TARGET_EMSCRIPTEN) && defined(WITH_WEBSOCKET)
		// TODO: Implement round trip time measurement for WebSocket connections
		return 0;
#else
		return (_state == NetworkState::Connected && !_peers.empty() ? _peers[0]->roundTripTime : 0);
#endif
	}

	Array<String> NetworkManagerBase::GetServerEndpoints() const
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		// Creating a server is not supported on Emscripten
		return {};
#else
		Array<String> result;

		if (_state == NetworkState::Listening) {
#	if defined(DEATH_TARGET_SWITCH)
			struct ifconf ifc;
			struct ifreq ifr[8];
			ifc.ifc_len = sizeof(ifr);
			ifc.ifc_req = ifr;

			if (ioctl(_host->socket, SIOCGIFCONF, &ifc) >= 0) {
				std::int32_t count = ifc.ifc_len / sizeof(struct ifreq);
				LOGI("[MP] Found {} interfaces:", count);
				for (std::int32_t i = 0; i < count; i++) {
					if (ifr[i].ifr_addr.sa_family == AF_INET) { // IPv4
						auto* addrPtr = &((struct sockaddr_in*)&ifr[i].ifr_addr)->sin_addr;
						String addressString = AddressToString(*addrPtr, _host->address.port);
						LOGI("[MP] -\t{}: {}", ifr[i].ifr_name, addressString);
						if (!addressString.empty() && !addressString.hasPrefix("127.0.0.1:"_s)) {
							arrayAppend(result, std::move(addressString));
						}
					}
#		if ENET_IPV6
					else if (ifr[i].ifr_addr.sa_family == AF_INET6) { // IPv6
						auto* addrPtr = &((struct sockaddr_in6*)&ifr[i].ifr_addr)->sin6_addr;
						//auto scopeId = ((struct sockaddr_in6*)&ifr[i].ifr_addr)->sin6_scope_id;
						String addressString = AddressToString(*addrPtr, /*scopeId*/0, _host->address.port);
						LOGI("[MP] -\t{}: {}", ifr[i].ifr_name, addressString);
						if (!addressString.empty() && !addressString.hasPrefix("[::1]:"_s)) {
							arrayAppend(result, std::move(addressString));
						}
					}
#		endif
				}
			} else {
				LOGW("[MP] Failed to get server endpoints");
			}
#	elif defined(DEATH_TARGET_WINDOWS)
			ULONG bufferSize = 0;
			::GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, NULL, &bufferSize);
			std::unique_ptr<std::uint8_t[]> buffer = std::make_unique<std::uint8_t[]>(bufferSize);
			auto* adapterAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.get());

			if (::GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, adapterAddresses, &bufferSize) == NO_ERROR) {
				for (auto* adapter = adapterAddresses; adapter != NULL; adapter = adapter->Next) {
					for (auto* address = adapter->FirstUnicastAddress; address != NULL; address = address->Next) {
						String addressString;
						if (address->Address.lpSockaddr->sa_family == AF_INET) { // IPv4
							auto* addrPtr = &((struct sockaddr_in*)address->Address.lpSockaddr)->sin_addr;
							String addressString = AddressToString(*addrPtr, _host->address.port);
							if (!addressString.empty() && !addressString.hasPrefix("127.0.0.1:"_s)) {
								arrayAppend(result, std::move(addressString));
							}
						}
#		if ENET_IPV6
						else if (address->Address.lpSockaddr->sa_family == AF_INET6) { // IPv6
							auto* addrPtr = &((struct sockaddr_in6*)address->Address.lpSockaddr)->sin6_addr;
							//auto scopeId = ((struct sockaddr_in6*)address->Address.lpSockaddr)->sin6_scope_id;
							String addressString = AddressToString(*addrPtr, /*scopeId*/0, _host->address.port);
							if (!addressString.empty() && !addressString.hasPrefix("[::1]:"_s)) {
								arrayAppend(result, std::move(addressString));
							}
						}
#		endif
						else {
							// Unsupported address family
						}
					}
				}
			} else {
				LOGW("[MP] Failed to get server endpoints");
			}
#	else
			struct ifaddrs* ifAddrStruct = nullptr;
			if (getifaddrs(&ifAddrStruct) == 0) {
				for (struct ifaddrs* ifa = ifAddrStruct; ifa != nullptr; ifa = ifa->ifa_next) {
					if (ifa->ifa_addr == nullptr) {
						continue;
					}

					if (ifa->ifa_addr->sa_family == AF_INET) { // IPv4
						auto* addrPtr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
						String addressString = AddressToString(*addrPtr, _host->address.port);
						if (!addressString.empty() && !addressString.hasPrefix("127.0.0.1:"_s)) {
							arrayAppend(result, std::move(addressString));
						}
					}
#		if ENET_IPV6
					else if (ifa->ifa_addr->sa_family == AF_INET6) { // IPv6
						auto* addrPtr = &((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr;
						//auto scopeId = ((struct sockaddr_in6*)ifa->ifa_addr)->sin6_scope_id;
						String addressString = AddressToString(*addrPtr, /*scopeId*/0, _host->address.port);
						if (!addressString.empty() && !addressString.hasPrefix("[::1]:"_s)) {
							arrayAppend(result, std::move(addressString));
						}
					}
#		endif
				}
				freeifaddrs(ifAddrStruct);
			} else {
				LOGW("[MP] Failed to get server endpoints");
			}
#	endif
		} else {
			if (!_peers.empty()) {
				String addressString = AddressToString(_peers[0]->address, true);
				if (!addressString.empty() && !addressString.hasPrefix("[::1]:"_s)) {
					arrayAppend(result, std::move(addressString));
				}
			}
		}

		return result;
#endif
	}

	std::uint16_t NetworkManagerBase::GetServerPort() const
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		// Creating a server is not supported on Emscripten
		return 0;
#else
		if (_state == NetworkState::Listening) {
			return _host->address.port;
		} else if (!_peers.empty()) {
			return _peers[0]->address.port;
		} else {
			return 0;
		}
#endif
	}

	void NetworkManagerBase::SendTo(const Peer& peer, NetworkChannel channel, std::uint8_t packetType, ArrayView<const std::uint8_t> data)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN) && defined(WITH_WEBSOCKET)
		if DEATH_LIKELY(_emWsSocket > 0) {
			SmallVector<std::uint8_t, 128> buf(1 + data.size());
			buf[0] = packetType;
			if (!data.empty()) {
				std::memcpy(buf.data() + 1, data.data(), data.size());
			}
			emscripten_websocket_send_binary(_emWsSocket, buf.data(), buf.size());
		}
#else
#	if defined(WITH_WEBSOCKET)
		if DEATH_UNLIKELY(peer.IsWebSocket()) {
			SendToWsPeer(peer.GetId(), packetType, data);
			return;
		}
#	endif

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

		ENetPacket* packet = enet_packet_create(packetType, data.data(), data.size(), flags);

		bool success;
		{
			std::unique_lock lock(_lock);
			success = enet_peer_send(target, std::uint8_t(channel), packet) >= 0;
		}

		if (!success) {
			enet_packet_destroy(packet);
		}
#endif
	}

	void NetworkManagerBase::SendTo(Function<bool(const Peer&)>&& predicate, NetworkChannel channel, std::uint8_t packetType, ArrayView<const std::uint8_t> data)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN) && defined(WITH_WEBSOCKET)
		if DEATH_LIKELY(_emWsSocket > 0) {
			Peer serverPeer = Peer::FromWebSocket(1);
			if (predicate(serverPeer)) {
				SmallVector<std::uint8_t, 128> buffer(1 + data.size());
				buffer[0] = packetType;
				if (!data.empty()) {
					std::memcpy(buffer.data() + 1, data.data(), data.size());
				}
				emscripten_websocket_send_binary(_emWsSocket, buffer.data(), buffer.size());
			}
		}
#else
		enet_uint32 flags;
		if (channel == NetworkChannel::Main) {
			flags = ENET_PACKET_FLAG_RELIABLE;
		} else {
			flags = ENET_PACKET_FLAG_UNSEQUENCED;
		}

		if (!_peers.empty()) {
			ENetPacket* packet = enet_packet_create(packetType, data.data(), data.size(), flags);

			bool success = false;
			{
				std::unique_lock lock(_lock);
				for (ENetPeer* peer : _peers) {
					if (predicate(Peer(peer))) {
						if (enet_peer_send(peer, std::uint8_t(channel), packet) >= 0) {
							success = true;
						}
					}
				}
			}

			if (!success) {
				enet_packet_destroy(packet);
			}
		}

#	if defined(WITH_WEBSOCKET)
		SmallVector<std::uint64_t, 16> wsTargets;
		{
			std::unique_lock<Spinlock> lock(_wsLock);
			for (auto& [id, info] : _wsPeers) {
				if (predicate(Peer::FromWebSocket(id))) {
					wsTargets.push_back(id);
				}
			}
		}
		for (auto id : wsTargets) {
			SendToWsPeer(id, packetType, data);
		}
#	endif
#endif
	}

	void NetworkManagerBase::SendTo(AllPeersT, NetworkChannel channel, std::uint8_t packetType, ArrayView<const std::uint8_t> data)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN) && defined(WITH_WEBSOCKET)
		if DEATH_LIKELY(_emWsSocket > 0) {
			SmallVector<std::uint8_t, 128> buffer(1 + data.size());
			buffer[0] = packetType;
			if (!data.empty()) {
				std::memcpy(buffer.data() + 1, data.data(), data.size());
			}
			emscripten_websocket_send_binary(_emWsSocket, buffer.data(), buffer.size());
		}
#else
		enet_uint32 flags;
		if (channel == NetworkChannel::Main) {
			flags = ENET_PACKET_FLAG_RELIABLE;
		} else {
			flags = ENET_PACKET_FLAG_UNSEQUENCED;
		}

		if (!_peers.empty()) {
			ENetPacket* packet = enet_packet_create(packetType, data.data(), data.size(), flags);

			bool success = false;
			{
				std::unique_lock lock(_lock);
				for (ENetPeer* peer : _peers) {
					if (enet_peer_send(peer, std::uint8_t(channel), packet) >= 0) {
						success = true;
					}
				}
			}

			if (!success) {
				enet_packet_destroy(packet);
			}
		}

#	if defined(WITH_WEBSOCKET)
		SmallVector<std::uint64_t, 16> wsTargets;
		{
			std::unique_lock<Spinlock> lock(_wsLock);
			for (auto& [id, info] : _wsPeers) {
				wsTargets.push_back(id);
			}
		}
		for (auto id : wsTargets) {
			SendToWsPeer(id, packetType, data);
		}
#	endif
#endif
	}

	void NetworkManagerBase::Kick(const Peer& peer, Reason reason)
	{
#if defined(WITH_WEBSOCKET) && !defined(DEATH_TARGET_EMSCRIPTEN)
		if DEATH_UNLIKELY(peer.IsWebSocket()) {
			ix::WebSocket* ws = nullptr;
			{
				std::unique_lock<Spinlock> lock(_wsLock);
				auto it = _wsPeers.find(peer.GetId());
				if (it != _wsPeers.end()) {
					ws = it->second.webSocket;
				}
			}
			if (ws != nullptr) {
				ws->close(ReasonToWsCloseCode(reason), ReasonToString(reason));
			}
			return;
		}
#endif
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (peer != nullptr) {
			std::unique_lock lock(_lock);
			enet_peer_disconnect(peer._enet, std::uint32_t(reason));
		}
#endif
	}


	String NetworkManagerBase::AddressToString(const struct in_addr& address, std::uint16_t port)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN) && defined(WITH_WEBSOCKET)
		// TODO: Implement this properly on Emscripten
		return {};
#else
		char addressString[64];

		if (inet_ntop(AF_INET, &address, addressString, sizeof(addressString) - 1) == NULL) {
			return {};
		}

		std::size_t addressLength = strnlen(addressString, sizeof(addressString));
		if (port != 0) {
			addressLength = addressLength + formatInto({ &addressString[addressLength], sizeof(addressString) - addressLength }, ":{}", port);
		}
		return String(addressString, addressLength);
#endif
	}

#if ENET_IPV6
	String NetworkManagerBase::AddressToString(const struct in6_addr& address, std::uint16_t scopeId, std::uint16_t port)
	{
#	if defined(DEATH_TARGET_EMSCRIPTEN) && defined(WITH_WEBSOCKET)
		// TODO: Implement this properly on Emscripten
		return {};
#	else
		char addressString[92];
		std::size_t addressLength = 0;

		if (IN6_IS_ADDR_V4MAPPED(&address)) {
			struct in_addr buf;
			enet_inaddr_map6to4(&address, &buf);

			if (inet_ntop(AF_INET, &buf, addressString, sizeof(addressString) - 1) == NULL) {
				return {};
			}

			addressLength = strnlen(addressString, sizeof(addressString));
		} else {
			if (inet_ntop(AF_INET6, (void*)&address, &addressString[1], sizeof(addressString) - 3) == NULL) {
				return {};
			}

			addressString[0] = '[';
			addressLength = strnlen(addressString, sizeof(addressString));

			if (scopeId != 0) {
				addressLength += formatInto({ &addressString[addressLength], sizeof(addressString) - addressLength }, "%{}", scopeId);
			}

			addressString[addressLength] = ']';
			addressLength++;
		}

		if (port != 0) {
			addressLength += formatInto({ &addressString[addressLength], sizeof(addressString) - addressLength }, ":{}", port);
		}
		return String(addressString, addressLength);
#	endif
	}
#endif

#if !defined(DEATH_TARGET_EMSCRIPTEN)
	String NetworkManagerBase::AddressToString(const ENetAddress& address, bool includePort)
	{
#	if ENET_IPV6
		return AddressToString(address.host, address.sin6_scope_id, includePort ? address.port : 0);
#	else
		return AddressToString(*(const struct in_addr*)&address.host, includePort ? address.port : 0);
#	endif
	}
#endif

	String NetworkManagerBase::AddressToString(const Peer& peer) const
	{
#if defined(DEATH_TARGET_EMSCRIPTEN) && defined(WITH_WEBSOCKET)
		return ExtractHostFromWsUrl(_emWsUrl);
#else
#	if defined(WITH_WEBSOCKET)
		if DEATH_UNLIKELY(peer.IsWebSocket()) {
			std::unique_lock<Spinlock> lock(_wsLock);
			auto it = _wsPeers.find(peer.GetId());
			if (it != _wsPeers.end()) {
				return it->second.address;
			}
			return {};
		}
#	endif

		if DEATH_LIKELY(peer._enet != nullptr) {
			return AddressToString(peer._enet->address);
		}
		return {};
#endif
	}

	bool NetworkManagerBase::IsAddressValid(StringView address)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN) && defined(WITH_WEBSOCKET)
		// TODO: Implement this properly on Emscripten
		return true;
#else
		auto nullTerminatedAddress = String::nullTerminatedView(address);
#	if ENET_IPV6
		struct sockaddr_in sa;
		struct sockaddr_in6 sa6;
		return (inet_pton(AF_INET6, nullTerminatedAddress.data(), &(sa6.sin6_addr)) == 1)
			|| (inet_pton(AF_INET, nullTerminatedAddress.data(), &(sa.sin_addr)) == 1);
#	else
		struct sockaddr_in sa;
		return (inet_pton(AF_INET, nullTerminatedAddress.data(), &(sa.sin_addr)) == 1);
#	endif
#endif
	}

	bool NetworkManagerBase::IsDomainValid(StringView domain)
	{
		if (domain.empty() || domain.size() > 253) {
			return false;
		}

		while (!domain.empty()) {
			StringView end = domain.findOr('.', domain.end());
			StringView part = domain.prefix(end.begin());
			if (part.size() < 1 || part.size() > 63) {
				return false;
			}

			for (char c : part) {
				if (!isalnum(c) && c != '-') {
					return false;
				}
			}

			// Part can't start or end with hyphen
			if (part[0] == '-' || part[part.size() - 1] == '-') {
				return false;
			}

			if (end.begin() == domain.end()) {
				break;
			}
			domain = domain.suffix(end.begin() + 1);
		}

		return true;
	}

	bool NetworkManagerBase::TrySplitAddressAndPort(StringView input, StringView& address, std::uint16_t& port)
	{
		if (auto portSep = input.findLast(':')) {
			auto portString = input.suffix(portSep.begin() + 1);
			if (portString.contains(']')) {
				// Probably only IPv6 address (or some garbage)
				address = input;
				port = 0;
				return true;
			} else {
				// Address (or hostname) and port
				address = input.prefix(portSep.begin()).trimmed();
				if (address.hasPrefix('[') && address.hasSuffix(']')) {
					address = address.slice(1, address.size() - 1);
				}
				if (address.empty()) {
					return false;
				}

				auto portString = input.suffix(portSep.begin() + 1);
				port = std::uint16_t(stou32(portString.data(), portString.size()));
				return true;
			}
		} else {
			// Address (or hostname) only
			address = input.trimmed();
			if (address.hasPrefix('[') && address.hasSuffix(']')) {
				address = address.slice(1, address.size() - 1);
			}
			if (address.empty()) {
				return false;
			}

			port = 0;
			return true;
		}
	}

	const char* NetworkManagerBase::ReasonToString(Reason reason)
	{
		switch (reason) {
			case Reason::Disconnected: return "Client disconnected by user"; break;
			case Reason::InvalidParameter: return "Invalid parameter specified"; break;
			case Reason::IncompatibleVersion: return "Incompatible client version"; break;
			case Reason::AuthFailed: return "Authentication failed"; break;
			case Reason::InvalidPassword: return "Invalid password specified"; break;
			case Reason::InvalidPlayerName: return "Invalid player name specified"; break;
			case Reason::NotInWhitelist: return "Client is not in server whitelist"; break;
			case Reason::Requires3rdPartyAuthProvider: return "Server requires 3rd party authentication provider"; break;
			case Reason::ServerIsFull: return "Server is full or busy"; break;
			case Reason::ServerNotReady: return "Server is not ready yet"; break;
			case Reason::ServerStopped: return "Server is stopped for unknown reason"; break;
			case Reason::ServerStoppedForMaintenance: return "Server is stopped for maintenance"; break;
			case Reason::ServerStoppedForReconfiguration: return "Server is stopped for reconfiguration"; break;
			case Reason::ServerStoppedForUpdate: return "Server is stopped for update"; break;
			case Reason::ConnectionLost: return "Connection lost"; break;
			case Reason::ConnectionTimedOut: return "Connection timed out"; break;
			case Reason::Kicked: return "Kicked by server"; break;
			case Reason::Banned: return "Banned by server"; break;
			case Reason::CheatingDetected: return "Cheating detected"; break;
			case Reason::AssetStreamingNotAllowed: return "Downloading of assets is not allowed"; break;
			case Reason::Idle: return "Inactivity"; break;
			default: return "Unknown reason"; break;
		}
	}

	ConnectionResult NetworkManagerBase::OnPeerConnected(const Peer& peer, std::uint32_t clientData)
	{
		return _handler->OnPeerConnected(peer, clientData);
	}

	void NetworkManagerBase::OnPeerDisconnected(const Peer& peer, Reason reason)
	{
		_handler->OnPeerDisconnected(peer, reason);

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (peer && _state == NetworkState::Listening) {
			std::unique_lock lock(_lock);
			for (std::size_t i = 0; i < _peers.size(); i++) {
				if (peer == _peers[i]) {
					_peers.eraseUnordered(i);
					break;
				}
			}
		}
#endif
	}

	void NetworkManagerBase::InitializeBackend()
	{
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (++_initializeCount == 1) {
			std::int32_t error = enet_initialize();
			DEATH_ASSERT(error == 0, ("enet_initialize() failed with error {}", error), );
		}
#endif
	}

	void NetworkManagerBase::ReleaseBackend()
	{
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (--_initializeCount == 0) {
			enet_deinitialize();
		}
#endif
	}

#if defined(DEATH_TARGET_EMSCRIPTEN)

	EM_BOOL NetworkManagerBase::OnEmWsOpen(int eventType, const EmscriptenWebSocketOpenEvent* e, void* userData)
	{
		LOGI("[MP] WebSocket connection opened");

		NetworkManagerBase* _this = static_cast<NetworkManagerBase*>(userData);
		if (_this->_emWsSocket <= 0 || _this->_handler == nullptr) {
			return EM_TRUE;
		}
		_this->_state = NetworkState::Connected;
		Peer serverPeer = Peer::FromWebSocket(1);
		ConnectionResult result = _this->OnPeerConnected(serverPeer, _this->_clientData);
		if (!result.IsSuccessful()) {
			EMSCRIPTEN_WEBSOCKET_T socket = _this->_emWsSocket;
			_this->_emWsSocket = 0;
			_this->_state = NetworkState::None;
			emscripten_websocket_close(socket, ReasonToWsCloseCode(result.FailureReason), ReasonToString(result.FailureReason));
			emscripten_websocket_delete(socket);
			_this->_handler = nullptr;
		}
		return EM_TRUE;
	}

	EM_BOOL NetworkManagerBase::OnEmWsMessage(int eventType, const EmscriptenWebSocketMessageEvent* e, void* userData)
	{
		NetworkManagerBase* _this = static_cast<NetworkManagerBase*>(userData);
		if (_this->_emWsSocket <= 0 || _this->_handler == nullptr) {
			return EM_TRUE;
		}
		if (!e->isText && e->numBytes >= 1) {
			_this->_handler->OnPacketReceived(Peer::FromWebSocket(1), 0,
				(std::uint8_t)e->data[0], arrayView((const std::uint8_t*)e->data + 1, e->numBytes - 1));
		}
		return EM_TRUE;
	}

	EM_BOOL NetworkManagerBase::OnEmWsError(int eventType, const EmscriptenWebSocketErrorEvent* e, void* userData)
	{
		LOGE("[MP] WebSocket connection error");
		return EM_TRUE;
	}

	EM_BOOL NetworkManagerBase::OnEmWsClose(int eventType, const EmscriptenWebSocketCloseEvent* e, void* userData)
	{
		NetworkManagerBase* _this = static_cast<NetworkManagerBase*>(userData);
		if (_this->_emWsSocket <= 0 || _this->_handler == nullptr) {
			return EM_TRUE;
		}
		EMSCRIPTEN_WEBSOCKET_T socket = _this->_emWsSocket;
		bool wasConnected = (_this->_state == NetworkState::Connected);
		_this->_emWsSocket = 0;
		_this->_state = NetworkState::None;
		emscripten_websocket_delete(socket);
		_this->OnPeerDisconnected(
			wasConnected ? Peer::FromWebSocket(1) : Peer{},
			WsCloseCodeToReason(e->code, wasConnected));
		_this->_handler = nullptr;
		return EM_TRUE;
	}

#else
#	if defined(WITH_WEBSOCKET)

	bool NetworkManagerBase::StartWsServer(std::uint16_t wsPort, StringView certPath, StringView keyPath)
	{
		if (wsPort == 0) {
			return false;
		}

		DEATH_ASSERT(_wsServer == nullptr, "WebSocket transport already started", false);

		_wsServer = std::make_unique<ix::WebSocketServer>(wsPort, "0.0.0.0");

#		if defined(WITH_WEBSOCKET_TLS)
		if (!certPath.empty() && !keyPath.empty()) {
			ix::SocketTLSOptions tlsOptions;
			tlsOptions.certFile = certPath;
			tlsOptions.keyFile = keyPath;
			tlsOptions.tls = true;
			_wsServer->setTLSOptions(tlsOptions);
			LOGI("[MP] WebSocket transport starting with TLS on port {}", wsPort);
		} else
#		endif
		{
			LOGI("[MP] WebSocket transport starting on port {}", wsPort);
		}

		_wsServer->setOnClientMessageCallback([this](std::shared_ptr<ix::ConnectionState> state,
			ix::WebSocket& ws, const ix::WebSocketMessagePtr& msg) {

			if (msg->type == ix::WebSocketMessageType::Open) {
				const std::uint64_t id = _nextWsId.fetch_add(1, std::memory_order_relaxed) + 1;
				const std::string stateId = state->getId();
				const std::string remoteIp = state->getRemoteIp();
				const std::uint32_t peerClientData = ParseQueryParamUInt32(
					StringView(msg->openInfo.uri.data(), msg->openInfo.uri.size()), "clientData"_s);

				{
					std::unique_lock<Spinlock> lock(_wsLock);
					_wsPeers.emplace(id, WsPeerInfo{&ws, String(remoteIp.data(), remoteIp.size())});
					_wsConnectionIds.emplace(stateId, id);
					_wsPendingEvents.push_back({WsQueuedEvent::Type::Open, id, {}, peerClientData, 0});
				}
				LOGD("[MP] WebSocket client connected [W|{:.6x}] from {}", id, StringView{remoteIp});

			} else if (msg->type == ix::WebSocketMessageType::Close) {
				const std::string stateId = state->getId();
				std::uint64_t id = 0;
				{
					std::unique_lock<Spinlock> lock(_wsLock);
					auto it = _wsConnectionIds.find(stateId);
					if (it != _wsConnectionIds.end()) {
						id = it->second;
						_wsPeers.erase(id);
						_wsConnectionIds.erase(it);
						_wsPendingEvents.push_back({WsQueuedEvent::Type::Close, id, {}, 0, std::uint16_t(msg->closeInfo.code)});
					}
				}
				if (id != 0) {
					LOGD("[MP] WebSocket client disconnected [W{:.7x}]", id);
				}

			} else if (msg->type == ix::WebSocketMessageType::Message && msg->binary) {
				const std::string stateId = state->getId();
				std::uint64_t id = 0;
				{
					std::unique_lock<Spinlock> lock(_wsLock);
					auto it = _wsConnectionIds.find(stateId);
					if (it != _wsConnectionIds.end()) {
						id = it->second;
					}
				}
				if (id != 0 && !msg->str.empty()) {
					std::unique_lock<Spinlock> lock(_wsLock);
					_wsPendingEvents.push_back({WsQueuedEvent::Type::Message, id, msg->str});
				}

			} else if (msg->type == ix::WebSocketMessageType::Error) {
				LOGE("[MP] WebSocket transport error: {}", StringView{msg->errorInfo.reason});
			}
		});

		auto res = _wsServer->listen();
		if (!res.first) {
			LOGE("[MP] WebSocket transport failed to listen on port {}: {}", wsPort, StringView{res.second});
			_wsServer = nullptr;
			return false;
		}

		_wsServer->start();
		return true;
	}

	void NetworkManagerBase::ProcessWsQueue(INetworkHandler* handler)
	{
		SmallVector<WsQueuedEvent, 0> pending;
		{
			std::unique_lock<Spinlock> lock(_wsLock);
			std::swap(pending, _wsPendingEvents);
		}

		for (auto& ev : pending) {
			switch (ev.type) {
				case WsQueuedEvent::Type::Open: {
					Peer wsPeer = Peer::FromWebSocket(ev.peerId);
					ConnectionResult result = OnPeerConnected(wsPeer, ev.clientData);
					if (!result.IsSuccessful()) {
						// Reject the connection
						ix::WebSocket* ws = nullptr;
						{
							std::unique_lock<Spinlock> lock(_wsLock);
							auto it = _wsPeers.find(ev.peerId);
							if (it != _wsPeers.end()) {
								ws = it->second.webSocket;
							}
						}
						if (ws != nullptr) {
							ws->close(ReasonToWsCloseCode(result.FailureReason), ReasonToString(result.FailureReason));
						}
					}
					break;
				}
				case WsQueuedEvent::Type::Close: {
					OnPeerDisconnected(Peer::FromWebSocket(ev.peerId), WsCloseCodeToReason(ev.closeCode, true));
					break;
				}
				case WsQueuedEvent::Type::Message: {
					if (ev.data.size() >= 1) {
						handler->OnPacketReceived(Peer::FromWebSocket(ev.peerId), 0,
							(std::uint8_t)ev.data[0], arrayView((const std::uint8_t*)ev.data.data() + 1, ev.data.size() - 1));
					}
					break;
				}
			}
		}
	}

	bool NetworkManagerBase::SendToWsPeer(std::uint64_t peerId, std::uint8_t packetType, ArrayView<const std::uint8_t> data)
	{
		ix::WebSocket* ws = nullptr;
		{
			std::unique_lock<Spinlock> lock(_wsLock);
			auto it = _wsPeers.find(peerId);
			if DEATH_UNLIKELY(it == _wsPeers.end()) {
				return false;
			}

			ws = it->second.webSocket;
		}

		std::string packet(1 + data.size(), '\0');
		packet[0] = (char)packetType;
		if (!data.empty()) {
			std::memcpy(&packet[1], data.data(), data.size());
		}
		ws->sendBinary(packet);
		return true;
	}

	void NetworkManagerBase::OnClientWsThread(void* param)
	{
		Thread::SetCurrentName("Multiplayer WebSocket client");

		NetworkManagerBase* _this = static_cast<NetworkManagerBase*>(param);
		INetworkHandler* handler = _this->_handler;

		bool wasConnected = false;
		Reason disconnectReason = Reason::ConnectionTimedOut;

		while (_this->_state != NetworkState::None) {
			Thread::Sleep(ProcessingIntervalMs);

			SmallVector<WsQueuedEvent, 0> pending;
			{
				std::unique_lock<Spinlock> lock(_this->_wsLock);
				std::swap(pending, _this->_wsPendingEvents);
			}

			for (auto& ev : pending) {
				switch (ev.type) {
					case WsQueuedEvent::Type::Open: {
						wasConnected = true;
						disconnectReason = Reason::Unknown;
						_this->_state = NetworkState::Connected;
						Peer serverPeer = Peer::FromWebSocket(ev.peerId);
						ConnectionResult result = _this->OnPeerConnected(serverPeer, ev.clientData);
						if (!result.IsSuccessful()) {
							ix::WebSocket* ws = nullptr;
							{
								std::unique_lock<Spinlock> lock(_this->_wsLock);
								auto it = _this->_wsPeers.find(ev.peerId);
								if (it != _this->_wsPeers.end()) {
									ws = it->second.webSocket;
								}
							}
							if (ws != nullptr) {
								ws->close(ReasonToWsCloseCode(result.FailureReason), ReasonToString(result.FailureReason));
							}
						}
						break;
					}
					case WsQueuedEvent::Type::Close: {
						disconnectReason = WsCloseCodeToReason(ev.closeCode, wasConnected);
						_this->_state = NetworkState::None;
						break;
					}
					case WsQueuedEvent::Type::Message: {
						if (ev.data.size() >= 1) {
							handler->OnPacketReceived(Peer::FromWebSocket(ev.peerId), 0,
								(std::uint8_t)ev.data[0], arrayView((const std::uint8_t*)ev.data.data() + 1, ev.data.size() - 1));
						}
						break;
					}
				}
			}
		}

		if (wasConnected) {
			_this->OnPeerDisconnected(Peer::FromWebSocket(1), disconnectReason);
		} else {
			_this->OnPeerDisconnected({}, disconnectReason);
		}

		_this->_handler = nullptr;
		_this->_thread.Detach();

		LOGD("[MP] WebSocket client thread exited");
	}
#	endif

	void NetworkManagerBase::OnClientThread(void* param)
	{
		Thread::SetCurrentName("Multiplayer client");

		NetworkManagerBase* _this = static_cast<NetworkManagerBase*>(param);
		INetworkHandler* handler = _this->_handler;

		ENetHost* host = nullptr;
		_this->_host = host;

		// Try to connect to each specified endpoint
		ENetEvent ev{};
		for (std::int32_t i = 0; i < std::int32_t(_this->_desiredEndpoints.size()) && _this->_state != NetworkState::None; i++) {
			ENetAddress& addr = _this->_desiredEndpoints[i];
			LOGI("[MP] Connecting to {} ({}/{})", _this->AddressToString(addr, true), i + 1, _this->_desiredEndpoints.size());
			
			if (host != nullptr) {
				enet_host_destroy(host);
			}

			host = enet_host_create(nullptr, 1, std::size_t(NetworkChannel::Count), 0, 0);
			_this->_host = host;
			if (host == nullptr) {
				LOGE("[MP] Failed to create client");
				_this->OnPeerDisconnected({}, Reason::InvalidParameter);
				return;
			}

			ENetPeer* peer = enet_host_connect(host, &addr, std::size_t(NetworkChannel::Count), _this->_clientData);
			if (peer == nullptr) {
				continue;
			}

			std::int32_t n = 10;
			while (n > 0) {
				if (_this->_state == NetworkState::None) {
					n = 0;
					break;
				}

				LOGD("enet_host_service() is trying to connect: {} ms", enet_time_get());
				if (enet_host_service(host, &ev, 1000) > 0 && ev.type == ENET_EVENT_TYPE_CONNECT) {
					break;
				}

				n--;
			}

			if (n != 0) {
				_this->_peers.push_back(ev.peer);
				break;
			}
		}

		Reason reason;
		if (_this->_peers.empty()) {
			LOGE("[MP] Failed to connect to the server");
			_this->_state = NetworkState::None;
			reason = Reason::ConnectionTimedOut;
		} else {
			_this->_state = NetworkState::Connected;
			_this->OnPeerConnected(ev.peer, ev.data);
			reason = Reason::Unknown;

			while DEATH_LIKELY(_this->_state != NetworkState::None) {
				std::int32_t result;
				{
					std::unique_lock lock(_this->_lock);
					result = enet_host_service(host, &ev, 0);
				}

				if DEATH_UNLIKELY(result <= 0) {
					if DEATH_UNLIKELY(result < 0) {
						LOGE("[MP] enet_host_service() returned {}", result);
						reason = Reason::ConnectionLost;
						break;
					}
					Thread::Sleep(ProcessingIntervalMs);
					continue;
				}

				switch (ev.type) {
					case ENET_EVENT_TYPE_RECEIVE: {
						auto data = arrayView(ev.packet->data, ev.packet->dataLength);
						handler->OnPacketReceived(ev.peer, ev.channelID, data[0], data.exceptPrefix(1));
						enet_packet_destroy(ev.packet);
						break;
					}
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

		if (!_this->_peers.empty()) {
			_this->OnPeerDisconnected(_this->_peers[0], reason);

			for (ENetPeer* peer : _this->_peers) {
				enet_peer_disconnect_now(peer, (std::uint32_t)Reason::Disconnected);
			}
			_this->_peers.clear();
		} else {
			_this->OnPeerDisconnected({}, reason);
		}

		enet_host_destroy(_this->_host);
		_this->_host = nullptr;
		_this->_handler = nullptr;

		_this->_thread.Detach();

		LOGD("[MP] Client thread exited: {} ({})", NetworkManagerBase::ReasonToString(reason), reason);
	}

	void NetworkManagerBase::OnServerThread(void* param)
	{
		Thread::SetCurrentName("Multiplayer server");

		NetworkManagerBase* _this = static_cast<NetworkManagerBase*>(param);
		INetworkHandler* handler = _this->_handler;
		ENetHost* host = _this->_host;

		_this->_peers.reserve(16);

		ENetEvent ev{};
		while DEATH_LIKELY(_this->_state != NetworkState::None) {
			std::int32_t result;
			{
				std::unique_lock lock(_this->_lock);
				result = enet_host_service(host, &ev, 0);
			}

			if DEATH_UNLIKELY(result <= 0) {
				if DEATH_UNLIKELY(result < 0) {
					LOGE("[MP] enet_host_service() returned {}", result);

					// Server failed, try to recreate it
					for (auto& peer : _this->_peers) {
						_this->OnPeerDisconnected(peer, Reason::ConnectionLost);
					}
					_this->_peers.clear();

					ENetAddress addr = host->address;
					{
						std::unique_lock lock(_this->_lock);
						enet_host_destroy(host);
						host = enet_host_create(&addr, MaxPeerCount, std::size_t(NetworkChannel::Count), 0, 0);
						_this->_host = host;
					}

					if (host == nullptr) {
						LOGE("[MP] Failed to recreate the server");
						break;
					}
				}
#	if defined(WITH_WEBSOCKET)
				_this->ProcessWsQueue(handler);
#	endif
				Thread::Sleep(ProcessingIntervalMs);
				continue;
			}

			switch (ev.type) {
				case ENET_EVENT_TYPE_CONNECT: {
					ConnectionResult result = _this->OnPeerConnected(ev.peer, ev.data);
					if DEATH_LIKELY(result.IsSuccessful()) {
						std::unique_lock lock(_this->_lock);
						bool alreadyExists = false;
						for (std::size_t i = 0; i < _this->_peers.size(); i++) {
							if (ev.peer == _this->_peers[i]) {
								alreadyExists = true;
								break;
							}
						}
						if DEATH_UNLIKELY(alreadyExists) {
							LOGW("Peer is already connected [{:.8x}]", std::uint64_t(ev.peer));
						} else {
							_this->_peers.push_back(ev.peer);
						}
					} else {
						std::unique_lock lock(_this->_lock);
						enet_peer_disconnect(ev.peer, std::uint32_t(result.FailureReason));
					}
					break;
				}
				case ENET_EVENT_TYPE_RECEIVE: {
					auto data = arrayView(ev.packet->data, ev.packet->dataLength);
					handler->OnPacketReceived(ev.peer, ev.channelID, data[0], data.exceptPrefix(1));
					enet_packet_destroy(ev.packet);
					break;
				}
				case ENET_EVENT_TYPE_DISCONNECT:
					_this->OnPeerDisconnected(ev.peer, Reason(ev.data));
					break;
				case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
					_this->OnPeerDisconnected(ev.peer, Reason::ConnectionLost);
					break;
			}

#	if defined(WITH_WEBSOCKET)
			_this->ProcessWsQueue(handler);
#	endif
		}

		for (ENetPeer* peer : _this->_peers) {
			enet_peer_disconnect_now(peer, std::uint32_t(Reason::ServerStopped));
		}
		_this->_peers.clear();

		enet_host_destroy(_this->_host);
		_this->_host = nullptr;
		_this->_handler = nullptr;

#	if defined(WITH_WEBSOCKET)
		if (_this->_wsServer != nullptr) {
			_this->_wsServer->stop();
			_this->_wsServer = nullptr;
		}
		if (_this->_wsClient != nullptr) {
			_this->_wsClient->stop();
			_this->_wsClient = nullptr;
		}
		{
			std::unique_lock<Spinlock> lock(_this->_wsLock);
			_this->_wsPeers.clear();
			_this->_wsConnectionIds.clear();
			_this->_wsPendingEvents.clear();
		}
#	endif

		_this->_thread.Detach();

		LOGD("[MP] Server thread exited");
	}

#endif

}

#endif