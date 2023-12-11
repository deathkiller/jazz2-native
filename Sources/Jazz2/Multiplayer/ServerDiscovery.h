#pragma once

#if defined(WITH_MULTIPLAYER)

#include "INetworkHandler.h"
#include "../../Common.h"
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
	struct ServerDesc
	{
		ENetAddress Endpoint;
		String EndpointString;
		std::uint8_t UniqueIdentifier[16];
		String Name;
		std::uint32_t GameModeAndFlags;
		std::uint32_t CurrentPlayers;
		std::uint32_t MaxPlayers;
		String LevelName;

		// TODO: LastPingTime
		//bool IsLost;
	};

	class IServerObserver
	{
	public:
		virtual void OnServerFound(ServerDesc&& desc) = 0;
	};

	class ServerDiscovery
	{
	public:
		static constexpr std::uint16_t DiscoveryPort = 7439;
		static constexpr std::int32_t UniqueIdentifierLength = 16;

		ServerDiscovery(INetworkHandler* server, std::uint16_t port);
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