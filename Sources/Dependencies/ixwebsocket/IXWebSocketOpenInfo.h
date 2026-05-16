/*
 *  IXWebSocketOpenInfo.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2017-2019 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include "IXWebSocketHttpHeaders.h"
#include <cstdint>
#include <string>

namespace ix
{
	/**
		@brief Information about a WebSocket open event.

		Holds URI, headers, and protocol for @ref WebSocket connection establishment.
	*/
	struct WebSocketOpenInfo
	{
		/** @brief WebSocket URI. */
		std::string uri;
		/** @brief HTTP headers. */
		WebSocketHttpHeaders headers;
		/** @brief Negotiated protocol. */
		std::string protocol;

		/**
		 * @brief Construct a new WebSocketOpenInfo.
		 * @param u URI string.
		 * @param h HTTP headers.
		 * @param p Protocol string.
		 */
		WebSocketOpenInfo(const std::string& u = std::string(),
						  const WebSocketHttpHeaders& h = WebSocketHttpHeaders(),
						  const std::string& p = std::string())
			: uri(u)
			, headers(h)
			, protocol(p)
		{
		}
	};
}
