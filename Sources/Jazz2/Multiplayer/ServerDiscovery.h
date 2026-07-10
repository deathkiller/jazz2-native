#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpGameMode.h"
#include "../PreferencesCache.h"

#include "../../nCine/Base/TimeStamp.h"

#if !defined(DEATH_TARGET_EMSCRIPTEN) || defined(DOXYGEN_GENERATING_OUTPUT)
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
#else
#	include <emscripten/fetch.h>
#	include <emscripten/html5.h>
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
		/** @brief Server version */
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

	private:
		ServerDiscovery(const ServerDiscovery&) = delete;
		ServerDiscovery& operator=(const ServerDiscovery&) = delete;

		NetworkManager* _server;
		IServerObserver* _observer;
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
#else
		static constexpr std::uint64_t PacketSignature = 0x2095A59FF0BFBBEF;

		ENetSocket _socket;
		Thread _thread;
		TimeStamp _lastLocalRequestTime;
		ENetAddress _localMulticastAddress;

		static ENetSocket TryCreateLocalSocket(const char* multicastAddress, ENetAddress& parsedAddress);

		void SendLocalDiscoveryRequest(ENetSocket socket, const ENetAddress& address);
		void DownloadPublicServerList(IServerObserver* observer);
		bool ProcessLocalDiscoveryResponses(ENetSocket socket, ServerDescription& discoveredServer, std::int32_t timeoutMs = 0);
		bool ProcessLocalDiscoveryRequests(ENetSocket socket, std::int32_t timeoutMs = 0);
		void SendLocalDiscoveryResponse(ENetSocket socket, NetworkManager* server);
		void PublishToPublicServerList(NetworkManager* server);
		void DelistFromPublicServerList(NetworkManager* server);

		static void OnClientThread(void* param);
		static void OnServerThread(void* param);
#endif
	};
}

#endif