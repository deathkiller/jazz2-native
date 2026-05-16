/*
 *  IXWebSocketMessage.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2017-2019 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include "IXWebSocketCloseInfo.h"
#include "IXWebSocketErrorInfo.h"
#include "IXWebSocketMessageType.h"
#include "IXWebSocketOpenInfo.h"
#include <memory>
#include <string>

namespace ix
{
	/**
		@brief Represents a WebSocket message and its metadata.

		Holds message type, payload, wire size, error, open, and close info for @ref WebSocket operations.
	*/
	struct WebSocketMessage
	{
		/** @brief Message type (see @ref WebSocketMessageType). */
		WebSocketMessageType type;
		/** @brief Message payload string (reference). */
		const std::string& str;
		/** @brief Size of the message on the wire. */
		size_t wireSize;
		/** @brief Error info for this message. */
		WebSocketErrorInfo errorInfo;
		/** @brief Open info for this message. */
		WebSocketOpenInfo openInfo;
		/** @brief Close info for this message. */
		WebSocketCloseInfo closeInfo;
		/** @brief True if message is binary. */
		bool binary;

		/**
		 * @brief Construct a new WebSocketMessage.
		 * @param t Message type.
		 * @param s Payload string.
		 * @param w Wire size.
		 * @param e Error info.
		 * @param o Open info.
		 * @param c Close info.
		 * @param b Binary flag.
		 */
		WebSocketMessage(WebSocketMessageType t,
						 const std::string& s,
						 size_t w,
						 WebSocketErrorInfo e,
						 WebSocketOpenInfo o,
						 WebSocketCloseInfo c,
						 bool b = false)
			: type(t)
			, str(s)
			, wireSize(w)
			, errorInfo(e)
			, openInfo(o)
			, closeInfo(c)
			, binary(b)
		{
		}

		/**
		 * @brief Deleted overload to prevent binding `str` to a temporary, which would cause
		 * undefined behavior since class members don't extend lifetime beyond the constructor call.
		 */
		WebSocketMessage(WebSocketMessageType t,
						 std::string&& s,
						 size_t w,
						 WebSocketErrorInfo e,
						 WebSocketOpenInfo o,
						 WebSocketCloseInfo c,
						 bool b = false) = delete;
	};

	/** @brief Unique pointer to a WebSocketMessage. */
	using WebSocketMessagePtr = std::unique_ptr<WebSocketMessage>;
}
