/*
 *  IXWebSocketHandshake.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2019 Machine Zone, Inc. All rights reserved.
 *  Copyright (c) 2026 Dan R.
 */

#pragma once

#include "IXCancellationRequest.h"
#include "IXHttp.h"
#include "IXSocket.h"
#include "IXWebSocketHttpHeaders.h"
#include "IXWebSocketInitResult.h"
#include "IXWebSocketPerMessageDeflate.h"
#include "IXWebSocketPerMessageDeflateOptions.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ix
{
	/**
		@brief Handles the WebSocket handshake process for client and server.

		Implements the RFC 6455 handshake, protocol selection, and per-message deflate negotiation.
		See also: @ref WebSocket, @ref WebSocketHttpHeaders, @ref WebSocketInitResult
	*/
	using ProtocolSelectionCallback =
		std::function<bool(const std::vector<std::string>& requestedProtocols,
						   std::string& selectedProtocol)>;

	class WebSocketHandshake
	{
	public:
		/**
		 * @brief Construct a new WebSocketHandshake.
		 * @param requestInitCancellation Cancellation flag.
		 * @param _socket Socket reference.
		 * @param perMessageDeflate Per-message deflate pointer.
		 * @param perMessageDeflateOptions Deflate options.
		 * @param enablePerMessageDeflate Enable per-message deflate.
		 * @param serverName Optional server name.
		 */
		WebSocketHandshake(std::atomic<bool>& requestInitCancellation,
						   std::unique_ptr<Socket>& _socket,
						   WebSocketPerMessageDeflatePtr& perMessageDeflate,
						   WebSocketPerMessageDeflateOptions& perMessageDeflateOptions,
						   std::atomic<bool>& enablePerMessageDeflate,
						   const std::string& serverName = std::string());

		/**
		 * @brief Perform the client-side WebSocket handshake.
		 * @param url WebSocket URL.
		 * @param extraHeaders Extra headers.
		 * @param protocol Protocol string.
		 * @param host Host string.
		 * @param path Path string.
		 * @param port Port number.
		 * @param timeoutSecs Timeout in seconds.
		 * @return @ref WebSocketInitResult
		 */
		WebSocketInitResult clientHandshake(const std::string& url,
											const WebSocketHttpHeaders& extraHeaders,
											const std::string& protocol,
											const std::string& host,
											const std::string& path,
											int port,
											int timeoutSecs);

		/**
		 * @brief Perform the server-side WebSocket handshake.
		 * @param timeoutSecs Timeout in seconds.
		 * @param enablePerMessageDeflate Enable per-message deflate.
		 * @param protocolSelectionCallback Protocol selection callback.
		 * @param request Optional HTTP request pointer.
		 * @return @ref WebSocketInitResult
		 */
		WebSocketInitResult serverHandshake(
			int timeoutSecs,
			bool enablePerMessageDeflate,
			const ProtocolSelectionCallback& protocolSelectionCallback = nullptr,
			HttpRequestPtr request = nullptr);

	private:
		/** @brief Generate a random string of given length. */
		std::string genRandomString(const int len);

		// Parse HTTP headers
		WebSocketInitResult sendErrorResponse(int code, const std::string& reason);

		bool insensitiveStringCompare(const std::string& a, const std::string& b);

		std::atomic<bool>& _requestInitCancellation;
		std::unique_ptr<Socket>& _socket;
		WebSocketPerMessageDeflatePtr& _perMessageDeflate;
		WebSocketPerMessageDeflateOptions& _perMessageDeflateOptions;
		std::atomic<bool>& _enablePerMessageDeflate;
		std::string _serverName;
	};
}
