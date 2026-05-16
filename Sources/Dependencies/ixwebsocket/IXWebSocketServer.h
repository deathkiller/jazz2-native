/*
 *  IXWebSocketServer.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2018 Machine Zone, Inc. All rights reserved.
 *  Copyright (c) 2026 Dan R.
 */

#pragma once

#include "IXSocketServer.h"
#include "IXWebSocket.h"
#include <condition_variable>
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
		@brief WebSocket server implementation based on @ref SocketServer.

		Handles WebSocket connections, protocol negotiation, message callbacks, and client management.
	*/
	class WebSocketServer : public SocketServer
	{
	public:
		/**
		 * @brief Callback type for new WebSocket connections.
		 * @param ws Weak pointer to @ref WebSocket.
		 * @param state Shared pointer to @ref ConnectionState.
		 */
		using OnConnectionCallback =
			std::function<void(std::weak_ptr<WebSocket>, std::shared_ptr<ConnectionState>)>;

		/**
		 * @brief Callback type for client messages.
		 * @param state Shared pointer to @ref ConnectionState.
		 * @param ws Reference to @ref WebSocket.
		 * @param msg Pointer to @ref WebSocketMessage.
		 */
		using OnClientMessageCallback = std::function<void(
			std::shared_ptr<ConnectionState>, WebSocket&, const WebSocketMessagePtr&)>;

		/**
		 * @brief Construct a new WebSocketServer.
		 * @param port Listening port.
		 * @param host Host address.
		 * @param backlog TCP backlog size.
		 * @param maxConnections Maximum allowed connections.
		 * @param handshakeTimeoutSecs Handshake timeout in seconds.
		 * @param addressFamily Address family.
		 * @param pingIntervalSeconds Ping interval in seconds.
		 * @param sendTimeoutSeconds Send timeout in seconds.
		 */
		WebSocketServer(int port = SocketServer::kDefaultPort,
						const std::string& host = SocketServer::kDefaultHost,
						int backlog = SocketServer::kDefaultTcpBacklog,
						size_t maxConnections = SocketServer::kDefaultMaxConnections,
						int handshakeTimeoutSecs = WebSocketServer::kDefaultHandShakeTimeoutSecs,
						int addressFamily = SocketServer::kDefaultAddressFamily,
						int pingIntervalSeconds = WebSocketServer::kPingIntervalSeconds,
						int sendTimeoutSeconds = WebSocketServer::kSendTimeoutSeconds);
		/** @brief Destructor. */
		virtual ~WebSocketServer();
		/** @brief Stop the server and all connections. */
		virtual void stop() final;

		/** @brief Enable automatic pong responses. */
		void enablePong();
		/** @brief Disable automatic pong responses. */
		void disablePong();
		/** @brief Disable per-message deflate compression. */
		void disablePerMessageDeflate();

		/**
		 * @brief Override the value sent in the `Server:` response header.
		 * @param serverName Custom server name string.
		 */
		void setServerName(const std::string& serverName);

		/**
		 * @brief Set a callback for protocol selection during handshake.
		 * @param callback Protocol selection callback (@ref ProtocolSelectionCallback).
		 *
		 * The callback receives the list of protocols requested by the client.
		 * Return true to accept (fill selectedProtocol with the chosen one, or
		 * leave it empty to accept with no protocol), or false to reject with HTTP 400.
		 */
		void setProtocolSelectionCallback(const ProtocolSelectionCallback& callback);

		/**
		 * @brief Set a callback for new connections.
		 * @param callback Connection callback (@ref OnConnectionCallback).
		 */
		void setOnConnectionCallback(const OnConnectionCallback& callback);
		/**
		 * @brief Set a callback for client messages.
		 * @param callback Message callback (@ref OnClientMessageCallback).
		 */
		void setOnClientMessageCallback(const OnClientMessageCallback& callback);

		/**
		 * @brief Get all currently connected clients.
		 * @return Set of shared pointers to @ref WebSocket.
		 */
		std::set<std::shared_ptr<WebSocket>> getClients();

		/** @brief Enable broadcast server mode. */
		void makeBroadcastServer();
		/** @brief Listen and start the server. */
		bool listenAndStart();

		/** @brief Default handshake timeout in seconds. */
		const static int kDefaultHandShakeTimeoutSecs;

		/** @brief Get the handshake timeout in seconds. */
		int getHandshakeTimeoutSecs();
		/** @brief Check if pong is enabled. */
		bool isPongEnabled();
		/** @brief Check if per-message deflate is enabled. */
		bool isPerMessageDeflateEnabled();

	private:
		// Member variables
		int _handshakeTimeoutSecs;
		bool _enablePong;
		bool _enablePerMessageDeflate;
		int _pingIntervalSeconds;
		int _sendTimeoutSeconds;
		std::string _serverName;
		ProtocolSelectionCallback _protocolSelectionCallback;

		OnConnectionCallback _onConnectionCallback;
		OnClientMessageCallback _onClientMessageCallback;

		std::mutex _clientsMutex;
		std::set<std::shared_ptr<WebSocket>> _clients;

		/** @brief Default enable pong flag. */
		const static bool kDefaultEnablePong;
		/** @brief Default ping interval in seconds. */
		const static int kPingIntervalSeconds;
		/** @brief Default send timeout in seconds. */
		const static int kSendTimeoutSeconds;

		// Methods
		/**
		 * @brief Handle a new client connection.
		 * @param socket Client socket.
		 * @param connectionState Connection state object.
		 */
		virtual void handleConnection(std::unique_ptr<Socket> socket,
									  std::shared_ptr<ConnectionState> connectionState);
		/** @brief Get the number of currently connected clients. */
		virtual size_t getConnectedClientsCount() final;

	protected:
		/**
		 * @brief Handle HTTP upgrade to WebSocket.
		 * @param socket Client socket.
		 * @param connectionState Connection state object.
		 * @param request Optional HTTP request pointer.
		 */
		void handleUpgrade(std::unique_ptr<Socket> socket,
						   std::shared_ptr<ConnectionState> connectionState,
						   HttpRequestPtr request = nullptr);
	};
}
