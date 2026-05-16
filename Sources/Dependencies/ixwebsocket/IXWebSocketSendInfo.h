/*
 *  IXWebSocketSendInfo.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2018 Machine Zone, Inc. All rights reserved.
 */

#pragma once

namespace ix
{
	/**
		@brief Information about a WebSocket send operation.
	
		Used by @ref ix::WebSocketTransport and @ref ix::WebSocket to report send status.
	*/
	struct WebSocketSendInfo
	{
		/** @brief True if the send operation succeeded. */
		bool success;
		/** @brief True if a compression error occurred. */
		bool compressionError;
		/** @brief Size of the payload sent (in bytes). */
		size_t payloadSize;
		/** @brief Size of the data on the wire (in bytes). */
		size_t wireSize;

		/**
		 * @brief Construct a WebSocketSendInfo.
		 * @param s Success flag
		 * @param c Compression error flag
		 * @param p Payload size
		 * @param w Wire size
		 */
		WebSocketSendInfo(bool s = false, bool c = false, size_t p = 0, size_t w = 0)
			: success(s)
			, compressionError(c)
			, payloadSize(p)
			, wireSize(w)
		{
		}
	};
}
