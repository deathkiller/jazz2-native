#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ConnectionResult.h"
#include "Peer.h"
#include "Reason.h"
#include "ServerDiscovery.h"
#include "../../Main.h"
#include "../../nCine/Threading/Thread.h"
#include "../../nCine/Threading/ThreadSync.h"

#include <Base/IDisposable.h>
#include <Containers/Function.h>
#include <Containers/SmallVector.h>
#include <Containers/StringView.h>
#include <IO/MemoryStream.h>
#include <Threading/Spinlock.h>

struct _ENetHost;

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
		/** @brief Converts the specified ENet address to the string representation */
		static String AddressToString(const ENetAddress& address);
		/** @brief Converts the endpoint of the specified peer to the string representation */
		static String AddressToString(const Peer& peer);
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

	private:
		static constexpr std::uint32_t ProcessingIntervalMs = 4;

		_ENetHost* _host;
		Thread _thread;
		NetworkState _state;
		std::uint32_t _clientData;
		SmallVector<_ENetPeer*, 1> _peers;
		INetworkHandler* _handler;
		SmallVector<ENetAddress, 0> _desiredEndpoints;
		Spinlock _lock;

		static void InitializeBackend();
		static void ReleaseBackend();

		static void OnClientThread(void* param);
		static void OnServerThread(void* param);
	};
}

#endif