/*
 *  IXWebSocketInitResult.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2019 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include "IXWebSocketHttpHeaders.h"

namespace ix
{
	/**
		@brief Result of a WebSocket handshake/init operation.

		Holds success flag, HTTP status, error string, headers, URI, and protocol for @ref WebSocketHandshake.
	*/
	struct WebSocketInitResult
	{
		/** @brief True if handshake/init succeeded. */
		bool success;
		/** @brief HTTP status code. */
		int http_status;
		/** @brief Error string if any. */
		std::string errorStr;
		/** @brief Parsed HTTP headers. */
		WebSocketHttpHeaders headers;
		/** @brief WebSocket URI. */
		std::string uri;
		/** @brief Negotiated protocol. */
		std::string protocol;

		/**
		 * @brief Construct a new WebSocketInitResult.
		 * @param s Success flag.
		 * @param status HTTP status code.
		 * @param e Error string.
		 * @param h HTTP headers.
		 * @param u URI string.
		 */
		WebSocketInitResult(bool s = false,
							int status = 0,
							const std::string& e = std::string(),
							WebSocketHttpHeaders h = WebSocketHttpHeaders(),
							const std::string& u = std::string())
		{
			success = s;
			http_status = status;
			errorStr = e;
			headers = h;
			uri = u;
			protocol = h["Sec-WebSocket-Protocol"];
		}
	};
}
