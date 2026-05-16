/*
 *  IXSocketServer.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2018 Machine Zone, Inc. All rights reserved.
 *  Copyright (c) 2026 Dan R.
 */

#pragma once

#include "IXConnectionState.h"
#include "IXNetSystem.h"
#include "IXSelectInterrupt.h"
#include "IXSocketTLSOptions.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility> // pair

namespace ix
{
	class Socket;

	/**
		@brief TCP socket server for handling multiple client connections.

		Each connection is managed by a separate worker thread. Provides methods to start, stop,
		and manage connections, as well as TLS support and connection state customization.
		
		See also: @ref ConnectionState, @ref Socket, @ref SocketTLSOptions
	*/
	class SocketServer
	{
	public:
		/**
		 * @brief Factory type for creating custom @ref ConnectionState objects.
		 */
		using ConnectionStateFactory = std::function<std::shared_ptr<ConnectionState>()>;

		/**
		 * @brief List of (@ref ConnectionState, `std::thread`) pairs for managing active connections.
		 */
		using ConnectionThreads =
			std::list<std::pair<std::shared_ptr<ConnectionState>, std::thread>>;

		/**
		 * @brief Construct a new SocketServer.
		 * @param port Listening port.
		 * @param host Host address to bind.
		 * @param backlog TCP backlog size.
		 * @param maxConnections Maximum allowed simultaneous connections.
		 * @param addressFamily Address family (e.g., `AF_INET`).
		 */
		SocketServer(int port = kDefaultPort,
			 const std::string& host = kDefaultHost,
			 int backlog = kDefaultTcpBacklog,
			 size_t maxConnections = kDefaultMaxConnections,
			 int addressFamily = kDefaultAddressFamily);

		/** @brief Destructor. */
		virtual ~SocketServer();

		/** @brief Stop the server and all connections. */
		virtual void stop();

		/**
		 * @brief Set a custom factory for @ref ConnectionState objects.
		 * @param connectionStateFactory Factory function.
		 */
		void setConnectionStateFactory(const ConnectionStateFactory& connectionStateFactory);

		/** @brief Default port number. */
		const static int kDefaultPort;
		/** @brief Default host address. */
		const static std::string kDefaultHost;
		/** @brief Default TCP backlog size. */
		const static int kDefaultTcpBacklog;
		/** @brief Default maximum number of connections. */
		const static size_t kDefaultMaxConnections;
		/** @brief Default address family. */
		const static int kDefaultAddressFamily;

		/** @brief Start the server. */
		void start();
		/**
		 * @brief Begin listening for incoming connections.
		 * @return Pair of (`bool` success, `std::string` error message).
		 */
		std::pair<bool, std::string> listen();
		/** @brief Wait for the server to finish. */
		void wait();

		/**
		 * @brief Set TLS options for secure connections.
		 * @param socketTLSOptions TLS configuration (@ref SocketTLSOptions).
		 */
		void setTLSOptions(const SocketTLSOptions& socketTLSOptions);

		/** @brief Get the server port. */
		int  getPort();
		/** @brief Get the server host. */
		std::string getHost();
		/** @brief Get the TCP backlog size. */
		int getBacklog();
		/** @brief Get the maximum number of connections. */
		std::size_t getMaxConnections();
		/** @brief Get the address family. */
		int getAddressFamily();
	protected:
		/** @brief Log an error message. */
		void logError(const std::string& str);
		/** @brief Log an informational message. */
		void logInfo(const std::string& str);

		/** @brief Stop accepting new connections. */
		void stopAcceptingConnections();

	private:
		/** @brief Listening port. */
		int _port;
		/** @brief Host address. */
		std::string _host;
		/** @brief TCP backlog size. */
		int _backlog;
		/** @brief Maximum allowed connections. */
		size_t _maxConnections;
		/** @brief Address family. */
		int _addressFamily;

		/** @brief Socket file descriptor for accepting connections. */
		socket_t _serverFd;

		/** @brief Flag to stop the server. */
		std::atomic<bool> _stop;

		/** @brief Mutex for logging. */
		std::mutex _logMutex;

		/** @brief Thread for accepting incoming connections. */
		std::thread _thread;
		/** @brief Main server loop. */
		void run();
		/** @brief Callback for when a connection is terminated. */
		void onSetTerminatedCallback();

		/** @brief Flag to stop the garbage collector thread. */
		std::atomic<bool> _stopGc;
		/** @brief Garbage collector thread for cleaning up terminated connections. */
		std::thread _gcThread;
		/** @brief Garbage collector loop. */
		void runGC();

		/** @brief List of active connection threads. */
		ConnectionThreads _connectionsThreads;
		/** @brief Mutex for connection threads list. */
		std::mutex _connectionsThreadsMutex;

		/** @brief Condition variable for server control thread. */
		std::condition_variable _conditionVariable;
		/** @brief Mutex for server control thread. */
		std::mutex _conditionVariableMutex;

		/** @brief Factory for creating @ref ConnectionState objects. */
		ConnectionStateFactory _connectionStateFactory;

		/**
		 * @brief Handle a new client connection (to be implemented by derived classes).
		 * @param socket The client socket.
		 * @param connectionState The connection state object.
		 */
		virtual void handleConnection(std::unique_ptr<Socket>,
									  std::shared_ptr<ConnectionState> connectionState) = 0;
		/** @brief Get the number of currently connected clients. */
		virtual size_t getConnectedClientsCount() = 0;

		/** @brief Close and join all terminated connection threads. */
		void closeTerminatedThreads();
		/** @brief Get the number of connection threads. */
		size_t getConnectionsThreadsCount();

		/** @brief TLS options for secure connections. */
		SocketTLSOptions _socketTLSOptions;

		/** @brief Interrupt object to wake up from `select`. */
		SelectInterruptPtr _acceptSelectInterrupt;

		/** @brief Condition variable for garbage collector thread. */
		std::condition_variable _conditionVariableGC;
		/** @brief Mutex for garbage collector thread. */
		std::mutex _conditionVariableMutexGC;
		/** @brief Flag to allow garbage collector to continue. */
		bool _canContinueGC{ false };
	};
}
