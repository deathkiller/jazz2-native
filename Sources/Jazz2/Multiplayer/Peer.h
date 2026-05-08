#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

#include <Containers/StringView.h>

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
			, _wsId(0)
#	endif
#else
			: _wsId(0)
#endif
		{
		}

#ifndef DOXYGEN_GENERATING_OUTPUT
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		Peer(_ENetPeer* peer) : _enet(peer)
#	if defined(WITH_WEBSOCKET)
			, _wsId(0)
#	endif
		{
		}
#endif

#if defined(WITH_WEBSOCKET)
		/** @brief Creates a WebSocket peer from a numeric connection identifier */
		static Peer FromWebSocket(std::uint64_t wsId) {
			Peer p;
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
			p._enet = nullptr;
#	endif
			p._wsId = wsId;
			return p;
		}

		/** @brief Returns `true` if the peer uses the WebSocket transport */
		bool IsWebSocket() const {
#	if defined(DEATH_TARGET_EMSCRIPTEN)
			return true;
#	else
			return (_enet == nullptr);
#	endif
		}
#endif
#endif

		inline bool operator==(const Peer& other) const {
#if defined(DEATH_TARGET_EMSCRIPTEN)
			return (_wsId == other._wsId);
#else
#	if defined(WITH_WEBSOCKET)
			if DEATH_UNLIKELY(_enet == nullptr && _wsId == other._wsId) {
				return true;
			}
#	endif
			return (_enet == other._enet);
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
#if defined(DEATH_TARGET_EMSCRIPTEN)
			return (_wsId != 0);
#else
#	if defined(WITH_WEBSOCKET)
			if DEATH_UNLIKELY(_wsId != 0) {
				return true;
			}
#	endif
			return (_enet != nullptr);
#endif
		}

		/** @brief Returns a unique numeric identifier for logging and serialization */
		std::uint64_t GetId() const {
#if defined(DEATH_TARGET_EMSCRIPTEN)
			return _wsId;
#else
#	if defined(WITH_WEBSOCKET)
			if DEATH_UNLIKELY(_enet == nullptr) {
				return _wsId;
			}
#	endif
			return reinterpret_cast<std::uint64_t>(_enet);
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
#endif
#endif
	};
}

namespace Death::Implementation
{
	template<> struct Formatter<Jazz2::Multiplayer::Peer> {
		static std::size_t format(const Containers::MutableStringView& buffer, const Jazz2::Multiplayer::Peer& value, FormatContext& context);
	};
}

#endif
