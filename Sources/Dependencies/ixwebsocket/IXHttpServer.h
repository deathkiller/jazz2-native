/*
 *  IXHttpServer.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2018 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include "IXHttp.h"
#include "IXWebSocket.h"
#include "IXWebSocketServer.h"
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility> // pair

namespace ix
{
	/**
		@brief HTTP server implementation based on @ref WebSocketServer.
	*/
	class HttpServer final : public WebSocketServer
	{
	public:
		/**
		 * @brief Callback type for handling HTTP connections.
		 */
		using OnConnectionCallback =
			std::function<HttpResponsePtr(HttpRequestPtr, std::shared_ptr<ConnectionState>)>;

		/**
		 * @brief Construct a new HttpServer.
		 */
		HttpServer(int port = SocketServer::kDefaultPort,
				   const std::string& host = SocketServer::kDefaultHost,
				   int backlog = SocketServer::kDefaultTcpBacklog,
				   size_t maxConnections = SocketServer::kDefaultMaxConnections,
				   int addressFamily = SocketServer::kDefaultAddressFamily,
				   int timeoutSecs = HttpServer::kDefaultTimeoutSecs,
				   int handshakeTimeoutSecs = WebSocketServer::kDefaultHandShakeTimeoutSecs);

		/**
		 * @brief Set the connection callback for HTTP requests.
		 */
		void setOnConnectionCallback(const OnConnectionCallback& callback);

		/**
		 * @brief Make this server a redirect server.
		 * @param redirectUrl The URL to redirect to.
		 */
		void makeRedirectServer(const std::string& redirectUrl);

		/**
		 * @brief Enable debug server mode.
		 */
		void makeDebugServer();

		/**
		 * @brief Get the HTTP timeout in seconds.
		 */
		int getTimeoutSecs();

	private:
		OnConnectionCallback _onConnectionCallback; /**< Connection callback. */

		const static int kDefaultTimeoutSecs; /**< Default timeout in seconds. */
		int _timeoutSecs; /**< Timeout in seconds. */

		/**
		 * @brief Handle an incoming connection.
		 */
		virtual void handleConnection(std::unique_ptr<Socket>,
									  std::shared_ptr<ConnectionState> connectionState) final;

		/**
		 * @brief Set the default connection callback.
		 */
		void setDefaultConnectionCallback();
	};
}
