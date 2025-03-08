#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpGameMode.h"
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
#include <Containers/StaticArray.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Multiplayer
{
	class NetworkManager;

	/** @brief Server description */
	struct ServerDescription
	{
#ifndef DOXYGEN_GENERATING_OUTPUT
		/** @brief Server endpoint */
		ENetAddress Endpoint;
#endif
		/** @brief Server endpoint in text format */
		String EndpointString;
		/** @brief Server unique identifier */
		StaticArray<16, std::uint8_t> UniqueIdentifier;
		/** @brief Server name */
		String Name;
		/** @brief Multiplayer game mode */
		MpGameMode GameMode;
		/** @brief Server flags */
		std::uint32_t Flags;
		/** @brief Current number of players */
		std::uint32_t CurrentPlayerCount;
		/** @brief Maximum number of players */
		std::uint32_t MaxPlayerCount;
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
		virtual void OnServerFound(ServerDescription&& desc) = 0;
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

		/** @brief Creates an instance to advertise a running local server */
		ServerDiscovery(NetworkManager* server);
		/** @brief Creates an instance to observe remote servers */
		ServerDiscovery(IServerObserver* observer);
		~ServerDiscovery();

	private:
		ServerDiscovery(const ServerDiscovery&) = delete;
		ServerDiscovery& operator=(const ServerDiscovery&) = delete;

		static constexpr std::uint64_t PacketSignature = 0x2095A59FF0BFBBEF;

		NetworkManager* _server;
		IServerObserver* _observer;
		ENetSocket _socket;
		Thread _thread;
		TimeStamp _lastRequest;
		ENetAddress _address;

		static ENetSocket TryCreateSocket(const char* multicastAddress, ENetAddress& parsedAddress);

		static void TrySendRequest(ENetSocket socket, const ENetAddress& address);
		static bool ProcessResponses(ENetSocket socket, ServerDescription& discoveredServer, std::int32_t timeoutMs = 0);
		static bool ProcessRequests(ENetSocket socket, std::int32_t timeoutMs = 0);

		static void OnClientThread(void* param);
		static void OnServerThread(void* param);
	};
}

#endif