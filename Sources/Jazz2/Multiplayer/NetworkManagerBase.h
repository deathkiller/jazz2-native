#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ConnectionResult.h"
#include "Peer.h"
#include "Reason.h"
#include "ServerDiscovery.h"
#include "../../nCine/Threading/Thread.h"
#include "../../nCine/Threading/ThreadSync.h"

#include <Base/IDisposable.h>
#include <Containers/Function.h>
#include <Containers/SmallVector.h>
#include <Containers/StringView.h>
#include <IO/MemoryStream.h>
#include <Threading/Spinlock.h>

#if defined(WITH_WEBSOCKET)
#	if defined(DEATH_TARGET_EMSCRIPTEN)
#		include <emscripten/websocket.h>
#	else
#		include <ixwebsocket/IXWebSocket.h>
#		include <ixwebsocket/IXWebSocketServer.h>
#	endif
#endif

#if !defined(DEATH_TARGET_EMSCRIPTEN)
struct _ENetHost;
#endif

using namespace Death::Containers;
using namespace Death::IO;
using namespace Death::Threading;
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
		Listening,			/**< Listening as server */
		Connecting,			/**< Connecting to server as client */
		Connected			/**< Connected to server as client */
	};

	/**
		@brief All connected peers tag type
	*/
	struct AllPeersT {
#ifndef DOXYGEN_GENERATING_OUTPUT
		struct Init {};
		// Explicit constructor to avoid ambiguous calls when using {}
		constexpr explicit AllPeersT(Init) {}
#endif
	};

	/**
		@brief Local peer tag type
	*/
	struct LocalPeerT {
#ifndef DOXYGEN_GENERATING_OUTPUT
		struct Init {};
		// Explicit constructor to avoid ambiguous calls when using {}
		constexpr explicit LocalPeerT(Init) {}
#endif
	};

	/**
		@brief All connected peers tag

		Use in @ref NetworkManagerBase::SendTo() to send to all connected peers or the remote server peer.
	*/
	constexpr AllPeersT AllPeers{AllPeersT::Init{}};

	/**
		@brief Local peer tag
	*/
	constexpr LocalPeerT LocalPeer{LocalPeerT::Init{}};

	/**
		@brief Allows to create generic network clients and servers
	*/
	class NetworkManagerBase : public Death::IDisposable
	{
		friend class ServerDiscovery;

	public:
		/** @{ @name Constants */

		/** @brief Maximum connected peer count */
		static constexpr std::uint32_t MaxPeerCount = 128;

		/** @} */

		NetworkManagerBase();
		~NetworkManagerBase();

		NetworkManagerBase(const NetworkManagerBase&) = delete;
		NetworkManagerBase& operator=(const NetworkManagerBase&) = delete;

		/** @brief Creates a client connection to a remote server */
		virtual void CreateClient(INetworkHandler* handler, StringView endpoints, std::uint16_t defaultPort, std::uint32_t clientData);
		/** @brief Creates a server that accepts incoming connections */
		virtual bool CreateServer(INetworkHandler* handler, std::uint16_t port);
		/** @brief Disposes all active connections */
		virtual void Dispose();

		/** @brief Returns state of network connection */
		NetworkState GetState() const;
		/** @brief Returns mean round trip time to the server, in milliseconds */
		std::uint32_t GetRoundTripTimeMs() const;
		/** @brief Returns all IPv4 and IPv6 addresses along with ports of the server */
		Array<String> GetServerEndpoints() const;
		/** @brief Returns port of the server */
		std::uint16_t GetServerPort() const;

		/** @brief Sends a packet to a given peer */
		void SendTo(const Peer& peer, NetworkChannel channel, std::uint8_t packetType, ArrayView<const std::uint8_t> data);
		/** @brief Sends a packet to all connected peers that match a given predicate */
		void SendTo(Function<bool(const Peer&)>&& predicate, NetworkChannel channel, std::uint8_t packetType, ArrayView<const std::uint8_t> data);
		/** @brief Sends a packet to all connected peers or the remote server peer */
		void SendTo(AllPeersT, NetworkChannel channel, std::uint8_t packetType, ArrayView<const std::uint8_t> data);
		/** @brief Kicks a given peer from the server */
		void Kick(const Peer& peer, Reason reason);

		/** @brief Converts the specified IPv4 endpoint to the string representation */
		static String AddressToString(const struct in_addr& address, std::uint16_t port = 0);
#if ENET_IPV6
		/** @brief Converts the specified IPv6 endpoint to the string representation */
		static String AddressToString(const struct in6_addr& address, std::uint16_t scopeId, std::uint16_t port = 0);
#endif
#if !defined(DEATH_TARGET_EMSCRIPTEN) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Converts the specified ENet address to the string representation */
		static String AddressToString(const ENetAddress& address, bool includePort = false);
#endif
		/** @brief Converts the endpoint of the specified peer to the string representation */
		static String AddressToString(const Peer& peer);
		/** @brief Returns the address string for the specified peer, supporting both ENet and WebSocket peers */
		String GetPeerAddress(const Peer& peer);
		/** @brief Returns `true` if the specified string representation of the address is valid */
		static bool IsAddressValid(StringView address);
		/** @brief Returns `true` if the specified string representation of the domain name is valid */
		static bool IsDomainValid(StringView domain);
		/** @brief Attempts to split the specified endpoint into address and port */
		static bool TrySplitAddressAndPort(StringView input, StringView& address, std::uint16_t& port);
		/** @brief Converts the specified reason to the string representation */
		static const char* ReasonToString(Reason reason);

	protected:
		/** @brief Called when a peer connects to the local server or the local client connects to a server */
		virtual ConnectionResult OnPeerConnected(const Peer& peer, std::uint32_t clientData);
		/** @brief Called when a peer disconnects from the local server or the local client disconnects from a server */
		virtual void OnPeerDisconnected(const Peer& peer, Reason reason);

#if defined(WITH_WEBSOCKET) && !defined(DEATH_TARGET_EMSCRIPTEN)
		/** @brief Starts a WebSocket server alongside the ENet server */
		bool StartWsServer(std::uint16_t wsPort, StringView certPath, StringView keyPath);
#endif

	private:
		static constexpr std::uint32_t ProcessingIntervalMs = 4;

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		_ENetHost* _host;
		Thread _thread;
		SmallVector<_ENetPeer*, 1> _peers;
		SmallVector<ENetAddress, 0> _desiredEndpoints;
#endif
		NetworkState _state;
		std::uint32_t _clientData;
		INetworkHandler* _handler;
		Spinlock _lock;

#if defined(WITH_WEBSOCKET) && !defined(DEATH_TARGET_EMSCRIPTEN)
		/** @brief Represents a connected WebSocket peer */
		struct WsPeerInfo {
			ix::WebSocket* webSocket;
			String address;
		};

		/** @brief Queued event from WebSocket callbacks to the main processing thread */
		struct WsQueuedEvent {
			enum class Type : std::uint8_t { Open, Close, Message };
			Type type;
			std::uint64_t peerId;
			std::string data;
		};

		std::unique_ptr<ix::WebSocketServer> _wsServer;
		std::unique_ptr<ix::WebSocket> _wsClient;
		HashMap<std::uint64_t, WsPeerInfo> _wsPeers;
		HashMap<std::string, std::uint64_t> _wsConnectionIds;
		SmallVector<WsQueuedEvent, 0> _wsPendingEvents;
		Spinlock _wsLock;
		std::atomic_uint64_t _nextWsId{1};

		void ProcessWsQueue(INetworkHandler* handler);
		bool SendToWsPeer(std::uint64_t peerId, std::uint8_t packetType, ArrayView<const std::uint8_t> data);
#elif defined(WITH_WEBSOCKET) && defined(DEATH_TARGET_EMSCRIPTEN)
		/** @brief Emscripten WebSocket handle (0 = not connected) */
		EMSCRIPTEN_WEBSOCKET_T _emWsSocket{0};
		/** @brief URL of the WebSocket server being connected to */
		String _emWsUrl;

		static EM_BOOL OnEmWsOpen(int eventType, const EmscriptenWebSocketOpenEvent* e, void* userData);
		static EM_BOOL OnEmWsMessage(int eventType, const EmscriptenWebSocketMessageEvent* e, void* userData);
		static EM_BOOL OnEmWsError(int eventType, const EmscriptenWebSocketErrorEvent* e, void* userData);
		static EM_BOOL OnEmWsClose(int eventType, const EmscriptenWebSocketCloseEvent* e, void* userData);
#endif

		static void InitializeBackend();
		static void ReleaseBackend();

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		static void OnClientThread(void* param);
		static void OnServerThread(void* param);
#endif
	};
}

#endif