#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpGameMode.h"
#include "../PreferencesCache.h"

#include "../../nCine/Base/TimeStamp.h"

#if defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DOXYGEN_GENERATING_OUTPUT)
#	include <emscripten/fetch.h>
#	include <emscripten/html5.h>
#elif defined(WITH_ONLINE_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)
#	include "../../nCine/Threading/Thread.h"
// <mmeapi.h> included by "enet.h" still uses `far` macro
#	define far

#	define ENET_FEATURE_ADDRESS_MAPPING
#	if defined(DEATH_DEBUG)
#		define ENET_DEBUG
#	endif
#	include <enet/enet.h>

// Undefine it again after include
#	undef far
#endif

#include <Base/TypeInfo.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Multiplayer
{
	class NetworkManager;

	/**
		@brief Server description
		
		Summary of a discovered server as advertised over LAN discovery or the public list, holding its
		endpoint, identity and version, current game mode and level, player counts and transport details.
		It is reported to an @ref IServerObserver and shown in the server browser.
	*/
	struct ServerDescription
	{
		/** @brief Server endpoint in text format */
		String EndpointString;
		/** @brief Server unique identifier */
		Uuid UniqueServerID;
		/** @brief Server protocol version, see @ref NCINE_PROTOCOL_VERSION */
		String Version;
		/** @brief Server name */
		String Name;
		/** @brief Multiplayer game mode */
		MpGameMode GameMode = MpGameMode::Unknown;
		/** @brief Server flags */
		std::uint32_t Flags = 0;
		/** @brief Current number of players */
		std::uint32_t CurrentPlayerCount = 0;
		/** @brief Maximum number of players */
		std::uint32_t MaxPlayerCount = 0;
		/** @brief Current level name */
		String LevelName;

#if defined(WITH_WEBSOCKET) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief WebSocket port (`0` = no WebSocket transport support) */
		std::uint16_t WsPort = 0;
		/** @brief Whether the WebSocket transport uses TLS (WSS) */
		bool WsSecure = false;
#endif

		/** @brief Whether the server is compatible with the local client */
		bool IsCompatible = false;

		// TODO: LastPingTime
		//bool IsLost;
	};

	/**
		@brief Interface to observe publicly-listed running servers
		
		@experimental
	*/
	class IServerObserver
	{
	public:
		/** @brief Called when a server is discovered */
		virtual void OnServerFound(ServerDescription&& desc) = 0;
	};

	/**
		@brief Interface to provide current status of the server
		
		@experimental
	*/
	class IServerStatusProvider
	{
		DEATH_RUNTIME_OBJECT();

	public:
		/** @brief Returns display name of current level */
		virtual StringView GetLevelDisplayName() const = 0;
	};

	/**
		@brief Allows to monitor publicly-listed running servers for server listing
		
		Runs on a background thread in one of two roles: when given a @ref NetworkManager it advertises the
		local server, answering UDP multicast discovery requests on the LAN and publishing the server to the
		public list; when given an @ref IServerObserver it sends discovery requests and downloads the public
		list, reporting each discovered server back to the observer.

		@experimental
	*/
	class ServerDiscovery
	{
	public:
		/** @{ @name Constants */

		/** @brief UDP port for server discovery broadcast */
		static constexpr std::uint16_t DiscoveryPort = 7439;
		/** @brief Length of server unique identifier */
		static constexpr std::int32_t UniqueIdentifierLength = 16;

		/** @} */

		/** @brief Creates an instance to advertise a running local server */
		ServerDiscovery(NetworkManager* server);
		/** @brief Creates an instance to observe remote servers */
		ServerDiscovery(IServerObserver* observer);
		~ServerDiscovery();

		/** @brief Sets status provider */
		void SetStatusProvider(std::weak_ptr<IServerStatusProvider> statusProvider);
		/**
		 * @brief Stops discovering and waits for the discovery thread to end
		 *
		 * The destructor does the same; this is for a caller that has to release the thread earlier - the
		 * server list calls it right before connecting, because on the PSP the thread-stack pool cannot hold
		 * the discovery thread and the multiplayer client thread at the same time (see @ref nCine::Thread).
		 */
		void Stop();

	private:
		ServerDiscovery(const ServerDiscovery&) = delete;
		ServerDiscovery& operator=(const ServerDiscovery&) = delete;

		NetworkManager* _server;
		IServerObserver* _observer;
		// Whether Stop() has run (it may be called before the destructor runs it again)
		bool _stopped = false;
		std::weak_ptr<IServerStatusProvider> _statusProvider;
		TimeStamp _lastOnlineRequestTime;
		bool _onlineSuccess;

#if defined(DEATH_TARGET_EMSCRIPTEN)
		emscripten_fetch_t* _pendingFetch;
		long _refreshTimerId;

		void DownloadPublicServerListAsync();

		static void OnFetchSuccess(emscripten_fetch_t* fetch);
		static void OnFetchError(emscripten_fetch_t* fetch);
		static void OnRefreshTimer(void* userData);
#elif defined(WITH_ONLINE_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)
		static constexpr std::uint64_t PacketSignature = 0x2095A59FF0BFBBEF;

		// Both are assigned by the discovery thread, but they are read by the loop conditions around it, so
		// they start out as "no socket" rather than as whatever the stack held
		ENetSocket _socket = ENET_SOCKET_NULL;
		Thread _thread;
		TimeStamp _lastLocalRequestTime;
		ENetAddress _localMulticastAddress = {};
#	if ENET_IPV6
		// The IPv4 half of local discovery, which exists only where the transport itself is IPv6: an IPv4-only
		// peer - every console, see ENET_IPV6=0 in the build - can neither join the multicast group above nor
		// be reached through it, and a dual-stack socket cannot be relied on to send to a broadcast address.
		// So this is a second, plain AF_INET socket, addressed through the IPv4-mapped form of ENetAddress.
		ENetSocket _socketV4 = ENET_SOCKET_NULL;
		ENetAddress _localBroadcastAddress = {};
		// Answering is throttled per address family, so a request over one never consumes the answer a client
		// on the other one is waiting for
		TimeStamp _lastLocalRequestTimeV4;

		static ENetSocket TryCreateLocalBroadcastSocket(ENetAddress& parsedAddress);
#	endif

		static ENetSocket TryCreateLocalSocket(const char* multicastAddress, ENetAddress& parsedAddress);

		// `ipv4` marks the socket as the IPv4 half of discovery in a build whose transport is IPv6 - the one
		// case where a socket's family is not the family ENet's own send and receive build addresses for. It
		// means nothing where ENet is IPv4 already (ENET_IPV6=0), because there the two halves are one thing.
		void SendLocalDiscoveryRequest(ENetSocket socket, const ENetAddress& address, bool ipv4);
		void DownloadPublicServerList(IServerObserver* observer);
		bool ProcessLocalDiscoveryResponses(ENetSocket socket, ServerDescription& discoveredServer, std::int32_t timeoutMs, bool ipv4);
		bool ProcessLocalDiscoveryRequests(ENetSocket socket, std::int32_t timeoutMs, bool ipv4);
		void SendLocalDiscoveryResponse(ENetSocket socket, const ENetAddress& address, NetworkManager* server, bool ipv4);
		void PublishToPublicServerList(NetworkManager* server);
		void DelistFromPublicServerList(NetworkManager* server);

		static void OnClientThread(void* param);
		static void OnServerThread(void* param);
#endif
	};
}

#endif