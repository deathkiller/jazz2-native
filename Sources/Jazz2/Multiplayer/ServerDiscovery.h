#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "INetworkHandler.h"
#include "../../Main.h"
#include "../../nCine/Base/TimeStamp.h"
#include "../../nCine/Threading/Thread.h"

// <mmeapi.h> included by "enet.h" still uses `far` macro
#define far

#define ENET_FEATURE_ADDRESS_MAPPING
#if defined(DEATH_DEBUG)
#	define ENET_DEBUG
#endif
#include "Backends/enet.h"

// Undefine it again after include
#undef far

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Multiplayer
{
	/** @brief Server description */
	struct ServerDesc
	{
#ifndef DOXYGEN_GENERATING_OUTPUT
		/** @brief Server endpoint */
		ENetAddress Endpoint;
#endif
		/** @brief Server endpoint in text format */
		String EndpointString;
		/** @brief Server unique identifier */
		std::uint8_t UniqueIdentifier[16];
		/** @brief Server name */
		String Name;
		/** @brief Game mode and flags */
		std::uint32_t GameModeAndFlags;
		/** @brief Current number of players */
		std::uint32_t CurrentPlayers;
		/** @brief Maximum number of players */
		std::uint32_t MaxPlayers;
		/** @brief Current level name */
		String LevelName;

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
		virtual void OnServerFound(ServerDesc&& desc) = 0;
	};

	/**
		@brief Allows to monitor publicly-listed running servers for server listing

		@experimental
	*/
	class ServerDiscovery
	{
	public:
		/** @brief UDP port for server discovery broadcast */
		static constexpr std::uint16_t DiscoveryPort = 7439;
		/** @brief Length of server unique identifier */
		static constexpr std::int32_t UniqueIdentifierLength = 16;

		/** @brief Creates an instance to advertise a running server */
		ServerDiscovery(INetworkHandler* server, std::uint16_t port);
		/** @brief Creates an instance to observe servers */
		ServerDiscovery(IServerObserver* observer);
		~ServerDiscovery();

	private:
		ServerDiscovery(const ServerDiscovery&) = delete;
		ServerDiscovery& operator=(const ServerDiscovery&) = delete;

		static constexpr std::uint64_t PacketSignature = 0x2095A59FF0BFBBEF;

		INetworkHandler* _server;
		IServerObserver* _observer;
		ENetSocket _socket;
		Thread _thread;
		TimeStamp _lastRequest;
		ENetAddress _address;
		std::uint16_t _actualPort;

		static ENetSocket TryCreateSocket(const char* multicastAddress, ENetAddress& parsedAddress);

		static void TrySendRequest(ENetSocket socket, const ENetAddress& address);
		static bool ProcessResponses(ENetSocket socket, ServerDesc& discoveredServer, std::int32_t timeoutMs = 0);
		static bool ProcessRequests(ENetSocket socket, std::int32_t timeoutMs = 0);

		static void OnClientThread(void* param);
		static void OnServerThread(void* param);
	};
}

#endif