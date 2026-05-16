/*
 *  IXWebSocketHttpHeaders.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2018 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include "IXCancellationRequest.h"
#include "IXStrCaseCompare.h"
#include <map>
#include <memory>
#include <string>

namespace ix
{
	class Socket;

	/**
		@brief Case-insensitive map for storing WebSocket HTTP headers.

		Used by @ref WebSocket and @ref WebSocketHandshake for header management.
	*/
	using WebSocketHttpHeaders = std::map<std::string, std::string, CaseInsensitiveLess>;

	/**
		@brief Parse HTTP headers from a socket stream.
		@param socket Socket to read from.
		@param isCancellationRequested Cancellation request callback.
		@return Pair of (`bool` success, @ref WebSocketHttpHeaders).
	*/
	std::pair<bool, WebSocketHttpHeaders> parseHttpHeaders(
		std::unique_ptr<Socket>& socket, const CancellationRequest& isCancellationRequested);
}
