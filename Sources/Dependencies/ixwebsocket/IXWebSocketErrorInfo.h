/*
 *  IXWebSocketErrorInfo.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2017-2018 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include <cstdint>
#include <string>

namespace ix
{
	/**
		@brief Information about a WebSocket error event.

		Holds retry count, wait time, HTTP status, reason, and decompression error flag for @ref WebSocket operations.
	*/
	struct WebSocketErrorInfo
	{
		/** @brief Number of retries. */
		uint32_t retries = 0;
		/** @brief Wait time in seconds. */
		double wait_time = 0;
		/** @brief HTTP status code. */
		int http_status = 0;
		/** @brief Error reason string. */
		std::string reason;
		/** @brief True if decompression error occurred. */
		bool decompressionError = false;
	};
}
