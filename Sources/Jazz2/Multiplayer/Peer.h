#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

#if !defined(DEATH_TARGET_EMSCRIPTEN) || defined(DOXYGEN_GENERATING_OUTPUT)
struct _ENetPeer;
#endif

namespace Jazz2::Multiplayer
{
	/** @brief Remote peer as opaque handle */
	struct Peer
	{
		Peer(std::nullptr_t = nullptr)
#if !defined(DEATH_TARGET_EMSCRIPTEN)
			: _enet(nullptr)
#	if defined(WITH_WEBSOCKET)
			, _wsId(0), _backend(0)
#	endif
#else
			: _wsId(0), _backend(0)
#endif
		{
		}

#ifndef DOXYGEN_GENERATING_OUTPUT
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		Peer(_ENetPeer* peer) : _enet(peer)
#	if defined(WITH_WEBSOCKET)
			, _wsId(0), _backend(0)
#	endif
		{
		}
#endif

#if defined(WITH_WEBSOCKET) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Creates a WebSocket peer from a numeric connection identifier */
		static Peer FromWebSocket(std::uint64_t wsId) {
			Peer p;
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
			p._enet = nullptr;
#	endif
			p._wsId = wsId;
			p._backend = 1;
			return p;
		}

		/** @brief Returns `true` if the peer uses the WebSocket transport */
		bool IsWebSocket() const {
			return _backend != 0;
		}

		/** @brief Returns the WebSocket connection identifier */
		std::uint64_t GetWebSocketId() const {
			return _wsId;
		}
#endif
#endif

		inline bool operator==(const Peer& other) const {
#if defined(WITH_WEBSOCKET)
			if (_backend != other._backend) return false;
			if (_backend != 0) return (_wsId == other._wsId);
#endif
#if !defined(DEATH_TARGET_EMSCRIPTEN)
			return (_enet == other._enet);
#else
			return false;
#endif
		}
		inline bool operator!=(const Peer& other) const {
			return !(*this == other);
		}

		explicit operator bool() const {
			return IsValid();
		}

		/** @brief Returns `true` if the peer is valid */
		bool IsValid() const {
#if defined(WITH_WEBSOCKET)
			if (_backend != 0) return (_wsId != 0);
#endif
#if !defined(DEATH_TARGET_EMSCRIPTEN)
			return (_enet != nullptr);
#else
			return false;
#endif
		}

		/** @brief Returns a unique numeric identifier for logging and serialization */
		std::uint64_t GetId() const {
#if defined(WITH_WEBSOCKET)
			if (_backend != 0) return _wsId;
#endif
#if !defined(DEATH_TARGET_EMSCRIPTEN)
			return reinterpret_cast<std::uint64_t>(_enet);
#else
			return 0;
#endif
		}

		/** @brief Returns mean round trip time to the peer in milliseconds (0 for WebSocket peers) */
		std::uint32_t GetRoundTripTime() const;

#ifndef DOXYGEN_GENERATING_OUTPUT
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		_ENetPeer* _enet;
#endif
#if defined(WITH_WEBSOCKET) || defined(DEATH_TARGET_EMSCRIPTEN)
		std::uint64_t _wsId;
		std::uint8_t _backend;		// 0 = ENet, 1 = WebSocket
		std::uint8_t _pad[7] = {};	// Explicit padding, zero-initialized for consistent hashing
#endif
#endif
	};
}

#if defined(WITH_WEBSOCKET) || defined(DEATH_TARGET_EMSCRIPTEN)

#include "../../nCine/Base/HashFunctions.h"

namespace nCine
{
	/** @brief Custom hash for Peer using the WebSocket connection identifier */
	template<>
	class xxHash64Func<Jazz2::Multiplayer::Peer>
	{
	public:
		hash64_t operator()(const Jazz2::Multiplayer::Peer& peer) const
		{
			std::uint64_t key = peer.GetId() ^ (static_cast<std::uint64_t>(peer._backend) << 56);
			return hash64_t(xxHash3(reinterpret_cast<const char*>(&key), sizeof(key)));
		}
	};
}

#endif

#endif
