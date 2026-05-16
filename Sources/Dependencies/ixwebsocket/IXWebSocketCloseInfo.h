/*
 *  IXWebSocketCloseInfo.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2017-2019 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include <cstdint>
#include <string>

namespace ix
{
	/**
		@brief Information about a WebSocket close event.

		Holds close code, reason, and remote flag for @ref WebSocket shutdowns.
	*/
	struct WebSocketCloseInfo
	{
		/** @brief Close code (see @ref WebSocketCloseConstants). */
		uint16_t code;
		/** @brief Close reason string. */
		std::string reason;
		/** @brief True if closed by remote peer. */
		bool remote;

		/**
		 * @brief Construct a new WebSocketCloseInfo.
		 * @param c Close code.
		 * @param r Reason string.
		 * @param rem Remote flag.
		 */
		WebSocketCloseInfo(uint16_t c = 0, const std::string& r = std::string(), bool rem = false)
			: code(c)
			, reason(r)
			, remote(rem)
		{
		}
	};
}
