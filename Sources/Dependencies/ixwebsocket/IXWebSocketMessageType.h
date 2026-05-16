/*
 *  IXWebSocketMessageType.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2017-2019 Machine Zone, Inc. All rights reserved.
 */

#pragma once

namespace ix
{
	/**
		@brief Enum for WebSocket message types.

		Used by @ref WebSocketMessage and @ref WebSocket to distinguish message events.
	*/
	enum class WebSocketMessageType
	{
		Message = 0,   /**< Regular message. */
		Open = 1,      /**< Connection opened. */
		Close = 2,     /**< Connection closed. */
		Error = 3,     /**< Error occurred. */
		Ping = 4,      /**< Ping frame. */
		Pong = 5,      /**< Pong frame. */
		Fragment = 6   /**< Fragmented message. */
	};
}
