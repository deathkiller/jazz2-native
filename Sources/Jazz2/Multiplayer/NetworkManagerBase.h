#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

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

struct _ENetHost;

using namespace Death::Containers;
using namespace Death::IO;
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
		Listening,			/**< Listening */
		Connecting,			/**< Connecting to server */
		Connected			/**< Connected to server */
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
		@brief All connected peers tag

		Use in @ref NetworkManagerBase::SendTo() to send to all connected peers or the remote server peer.
	*/
	constexpr AllPeersT AllPeers{AllPeersT::Init{}};

	/**
		@brief Allows to create generic network clients and servers
	*/
	class NetworkManagerBase : public Death::IDisposable
	{
		friend class ServerDiscovery;

	public:
		/** @brief Maximum connected peer count */
		static constexpr std::size_t MaxPeerCount = 128;

		NetworkManagerBase();
		~NetworkManagerBase();

		NetworkManagerBase(const NetworkManagerBase&) = delete;
		NetworkManagerBase& operator=(const NetworkManagerBase&) = delete;

		/** @brief Creates a client connection to a remote server */
		bool CreateClient(INetworkHandler* handler, StringView address, std::uint16_t port, std::uint32_t clientData);
		/** @brief Creates a server accepting that accepts incoming connections */
		bool CreateServer(INetworkHandler* handler, std::uint16_t port);
		/** @brief Disposes all active connections */
		void Dispose();

		/** @brief Returns state of network connection */
		NetworkState GetState() const;
		/** @brief Returns mean round trip time to the server, in milliseconds */
		std::uint32_t GetRoundTripTimeMs();

		/** @brief Sends a packet to a given peer */
		void SendTo(const Peer& peer, NetworkChannel channel, std::uint8_t packetType, ArrayView<const std::uint8_t> data);
		/** @brief Sends a packet to all connected peers that match a given predicate */
		void SendTo(Function<bool(const Peer&)>&& predicate, NetworkChannel channel, std::uint8_t packetType, ArrayView<const std::uint8_t> data);
		/** @brief Sends a packet to all connected peers or the remote server peer */
		void SendTo(AllPeersT, NetworkChannel channel, std::uint8_t packetType, ArrayView<const std::uint8_t> data);
		/** @brief Kicks a given peer from the server */
		void Kick(const Peer& peer, Reason reason);

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
		SmallVector<_ENetPeer*, 1> _peers;
		INetworkHandler* _handler;
		Mutex _lock;
		std::unique_ptr<ServerDiscovery> _discovery;

		static void InitializeBackend();
		static void ReleaseBackend();

		static void OnClientThread(void* param);
		static void OnServerThread(void* param);
	};
}

#endif