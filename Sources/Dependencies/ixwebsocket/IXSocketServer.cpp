/*
 *  IXSocketServer.cpp
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2018 Machine Zone, Inc. All rights reserved.
 *  Copyright (c) 2026 Dan R.
 */

#include "IXSocketServer.h"

#include "IXLogger.h"
#include "IXNetSystem.h"
#include "IXSelectInterrupt.h"
#include "IXSelectInterruptFactory.h"
#include "IXSetThreadName.h"
#include "IXSocket.h"
#include "IXSocketConnect.h"
#include "IXSocketFactory.h"
#include <assert.h>
#include <chrono>
#include <sstream>
#include <stdio.h>
#include <string.h>

namespace ix
{
	namespace
	{
		// How long the accept loop waits before trying again after an error that left the pending
		// connection in the queue, and how often such an error may be written to the log
		const int kAcceptRetryDelayInMs = 100;
		const int kErrorLogIntervalInSecs = 5;

		// How long a connection has to send its first byte when the transport is auto-detected
		const int kTlsAutoDetectTimeoutInMs = 5000;

		// Whether the error means that the process (or the kernel) is out of a resource that accepting
		// a connection needs. These are not per-connection failures - the connection stays queued, so
		// the next accept() fails the same way until a descriptor (or memory) is released elsewhere
		bool isResourceExhausted(int err)
		{
#ifdef _WIN32
			return (err == WSAEMFILE || err == WSAENOBUFS);
#else
			return (err == EMFILE || err == ENFILE || err == ENOBUFS || err == ENOMEM);
#endif
		}
	}

	const int SocketServer::kDefaultPort(8080);
	const std::string SocketServer::kDefaultHost("127.0.0.1");
	const int SocketServer::kDefaultTcpBacklog(5);
	const size_t SocketServer::kDefaultMaxConnections(128);
	const int SocketServer::kDefaultAddressFamily(AF_INET);

	SocketServer::SocketServer(
		int port, const std::string& host, int backlog, size_t maxConnections, int addressFamily)
		: _port(port)
		, _host(host)
		, _backlog(backlog)
		, _maxConnections(maxConnections)
		, _addressFamily(addressFamily)
		, _serverFd(-1)
		, _stop(false)
		, _stopGc(false)
		, _connectionStateFactory(&ConnectionState::createConnectionState)
		, _acceptSelectInterrupt(createSelectInterrupt())
	{
	}

	SocketServer::~SocketServer()
	{
		stop();
	}

	void SocketServer::logError(const char* functionName, const std::string& str)
	{
		log(LogLevel::Error, functionName, str);
	}

	void SocketServer::logInfo(const char* functionName, const std::string& str)
	{
		log(LogLevel::Info, functionName, str);
	}

	std::pair<bool, std::string> SocketServer::listen()
	{
		std::string acceptSelectInterruptInitErrorMsg;
		if (!_acceptSelectInterrupt->init(acceptSelectInterruptInitErrorMsg))
		{
			std::stringstream ss;
			ss << "SocketServer::listen() error in SelectInterrupt::init: "
			   << acceptSelectInterruptInitErrorMsg;

			return std::make_pair(false, ss.str());
		}

		if (_addressFamily != AF_INET && _addressFamily != AF_INET6)
		{
			std::string errMsg("SocketServer::listen() AF_INET and AF_INET6 are currently "
							   "the only supported address families");
			return std::make_pair(false, errMsg);
		}

		// Get a socket for accepting connections.
		if ((_serverFd = socket(_addressFamily, SOCK_STREAM, 0)) < 0)
		{
			std::stringstream ss;
			ss << "SocketServer::listen() error creating socket): " << strerror(Socket::getErrno());

			return std::make_pair(false, ss.str());
		}

		// Make that socket reusable. (allow restarting this server at will)
		int enable = 1;
		if (setsockopt(_serverFd, SOL_SOCKET, SO_REUSEADDR, (char*) &enable, sizeof(enable)) < 0)
		{
			std::stringstream ss;
			ss << "SocketServer::listen() error calling setsockopt(SO_REUSEADDR) "
			   << "at address " << _host << ":" << _port << " : " << strerror(Socket::getErrno());

			Socket::closeSocket(_serverFd);
			return std::make_pair(false, ss.str());
		}

		if (_addressFamily == AF_INET)
		{
			struct sockaddr_in server;
			memset(&server, '\0', sizeof(server));
			server.sin_family = _addressFamily;
			server.sin_port = htons(_port);

			if (ix::inet_pton(_addressFamily, _host.c_str(), &server.sin_addr.s_addr) <= 0)
			{
				std::stringstream ss;
				ss << "SocketServer::listen() error calling inet_pton "
				   << "at address " << _host << ":" << _port << " : "
				   << strerror(Socket::getErrno());

				Socket::closeSocket(_serverFd);
				return std::make_pair(false, ss.str());
			}

			// Bind the socket to the server address.
			if (bind(_serverFd, (struct sockaddr*) &server, sizeof(server)) < 0)
			{
				std::stringstream ss;
				ss << "SocketServer::listen() error calling bind "
				   << "at address " << _host << ":" << _port << " : "
				   << strerror(Socket::getErrno());

				Socket::closeSocket(_serverFd);
				return std::make_pair(false, ss.str());
			}
		}
		else // AF_INET6
		{
			struct sockaddr_in6 server;
			memset(&server, '\0', sizeof(server));
			server.sin6_family = _addressFamily;
			server.sin6_port = htons(_port);

			if (ix::inet_pton(_addressFamily, _host.c_str(), &server.sin6_addr) <= 0)
			{
				std::stringstream ss;
				ss << "SocketServer::listen() error calling inet_pton "
				   << "at address " << _host << ":" << _port << " : "
				   << strerror(Socket::getErrno());

				Socket::closeSocket(_serverFd);
				return std::make_pair(false, ss.str());
			}

			// Bind the socket to the server address.
			if (bind(_serverFd, (struct sockaddr*) &server, sizeof(server)) < 0)
			{
				std::stringstream ss;
				ss << "SocketServer::listen() error calling bind "
				   << "at address " << _host << ":" << _port << " : "
				   << strerror(Socket::getErrno());

				Socket::closeSocket(_serverFd);
				return std::make_pair(false, ss.str());
			}
		}

		//
		// Listen for connections. Specify the tcp backlog.
		//
		if (::listen(_serverFd, _backlog) < 0)
		{
			std::stringstream ss;
			ss << "SocketServer::listen() error calling listen "
			   << "at address " << _host << ":" << _port << " : " << strerror(Socket::getErrno());

			Socket::closeSocket(_serverFd);
			return std::make_pair(false, ss.str());
		}

		return std::make_pair(true, "");
	}

	void SocketServer::start()
	{
		_stop = false;

		if (!_thread.joinable())
		{
			_thread = std::thread(&SocketServer::run, this);
		}

		if (!_gcThread.joinable())
		{
			_gcThread = std::thread(&SocketServer::runGC, this);
		}
	}

	void SocketServer::wait()
	{
		std::unique_lock<std::mutex> lock(_conditionVariableMutex);
		_conditionVariable.wait(lock);
	}

	void SocketServer::stopAcceptingConnections()
	{
		_stop = true;
	}

	void SocketServer::stop()
	{
		// Stop accepting connections, and close the 'accept' thread
		if (_thread.joinable())
		{
			_stop = true;
			// Wake up select
			if (!_acceptSelectInterrupt->notify(SelectInterrupt::kCloseRequest))
			{
				logError(IX_CURRENT_FUNCTION, "Cannot wake up from select");
			}

			_thread.join();
			_stop = false;
		}

		// Join all threads and make sure that all connections are terminated
		if (_gcThread.joinable())
		{
			_stopGc = true;
			{
				std::lock_guard<std::mutex> lock{ _conditionVariableMutexGC };
				_canContinueGC = true;
			}
			_conditionVariableGC.notify_one();
			_gcThread.join();
			_stopGc = false;
		}

		_conditionVariable.notify_one();
		Socket::closeSocket(_serverFd);
	}

	void SocketServer::setConnectionStateFactory(
		const ConnectionStateFactory& connectionStateFactory)
	{
		_connectionStateFactory = connectionStateFactory;
	}

	//
	// join the threads for connections that have been closed
	//
	// When a connection is closed by a client, the connection state terminated
	// field becomes true, and we can use that to know that we can join that thread
	// and remove it from our _connectionsThreads data structure (a list).
	//
	void SocketServer::closeTerminatedThreads()
	{
		std::lock_guard<std::mutex> lock(_connectionsThreadsMutex);
		auto it = _connectionsThreads.begin();
		auto itEnd = _connectionsThreads.end();

		while (it != itEnd)
		{
			auto& connectionState = it->first;
			auto& thread = it->second;

			if (!connectionState->isTerminated())
			{
				++it;
				continue;
			}

			if (thread.joinable()) thread.join();
			it = _connectionsThreads.erase(it);
		}
	}

	void SocketServer::run()
	{
		// Set the socket to non blocking mode, so that accept calls are not blocking
		SocketConnect::configure(_serverFd);

		// Use a cryptic name to stay within the 16 bytes limit thread name limitation
		// $ echo Srv:gc:64000 | wc -c
		// 13
		setThreadName("Srv:ac:" + std::to_string(_port));

		// Neither the poll nor the accept below consumes the pending connection when it fails, so the
		// loop can run into the same error as fast as it can iterate. Writing every one of them to the
		// log would flood it (and spend a core doing so) for as long as the condition lasts, so the
		// message is emitted at most once every few seconds, with a count of the repeats in between.
		auto lastErrorTimePoint =
			std::chrono::steady_clock::now() - std::chrono::seconds(kErrorLogIntervalInSecs);
		uint64_t suppressedErrors = 0;

		auto logErrorThrottled = [this, &lastErrorTimePoint, &suppressedErrors](const std::string& str)
		{
			auto now = std::chrono::steady_clock::now();
			if (now - lastErrorTimePoint < std::chrono::seconds(kErrorLogIntervalInSecs))
			{
				suppressedErrors++;
				return;
			}

			lastErrorTimePoint = now;

			if (suppressedErrors == 0)
			{
				logError(IX_CURRENT_FUNCTION, str);
			}
			else
			{
				std::stringstream ss;
				ss << str << " (" << suppressedErrors << " more of these were suppressed)";
				suppressedErrors = 0;
				logError(IX_CURRENT_FUNCTION, ss.str());
			}
		};

		for (;;)
		{
			if (_stop) return;

			// Use poll to check whether a new connection is in progress
			int timeoutMs = -1;
#ifdef _WIN32
			// select cannot be interrupted on Windows so we need to pass a small timeout
			timeoutMs = 10;
#endif

			bool readyToRead = true;
			PollResultType pollResult =
				Socket::poll(readyToRead, timeoutMs, _serverFd, _acceptSelectInterrupt);

			if (pollResult == PollResultType::Error)
			{
				std::stringstream ss;
				ss << "error in select: " << strerror(Socket::getErrno());
				logErrorThrottled(ss.str());

				// A listening socket that stays in an error state (a closed or invalid descriptor,
				// for example) reports itself as ready over and over, so the loop has to be paced
				std::this_thread::sleep_for(std::chrono::milliseconds(kAcceptRetryDelayInMs));
				continue;
			}

			if (pollResult != PollResultType::ReadyForRead)
			{
				continue;
			}

			// Accept a connection.
			// Use sockaddr_storage to accommodate both AF_INET and AF_INET6 addresses.
			// sockaddr_in is only 16 bytes; sockaddr_in6 is 28 bytes. On Windows, passing
			// a too-small buffer to accept() causes WSAEFAULT (error 10014).
			struct sockaddr_storage client; // client address information (IPv4 or IPv6)
			int clientFd;                   // socket connected to client
			socklen_t addressLen = sizeof(client);
			memset(&client, 0, sizeof(client));

			if ((clientFd = accept(_serverFd, (struct sockaddr*) &client, &addressLen)) < 0)
			{
				if (!Socket::isWaitNeeded())
				{
					// FIXME: that error should be propagated
					int err = Socket::getErrno();
					std::stringstream ss;
					ss << "error accepting connection: " << err << ", " << strerror(err);
					logErrorThrottled(ss.str());

					if (isResourceExhausted(err))
					{
						// The connection is still waiting in the queue and every retry fails the same
						// way, so the loop has to wait for the resource to be released elsewhere
						// instead of spinning on it
						std::this_thread::sleep_for(std::chrono::milliseconds(kAcceptRetryDelayInMs));
					}
				}
				continue;
			}

			if (getConnectedClientsCount() >= _maxConnections)
			{
				std::stringstream ss;
				ss << "reached max connections = " << _maxConnections << ". "
				   << "Not accepting connection";
				logErrorThrottled(ss.str());

				Socket::closeSocket(clientFd);

				continue;
			}

			// Retrieve connection info, the ip address of the remote peer/client)
			std::string remoteIp;
			int remotePort;

			if (_addressFamily == AF_INET)
			{
				char remoteIp4[INET_ADDRSTRLEN];
				auto* client4 = reinterpret_cast<struct sockaddr_in*>(&client);
				if (ix::inet_ntop(AF_INET, &client4->sin_addr, remoteIp4, INET_ADDRSTRLEN) == nullptr)
				{
					int err = Socket::getErrno();
					std::stringstream ss;
					ss << "error calling inet_ntop (ipv4): " << err << ", " << strerror(err);
					logError(IX_CURRENT_FUNCTION, ss.str());

					Socket::closeSocket(clientFd);

					continue;
				}

				remotePort = ix::network_to_host_short(client4->sin_port);
				remoteIp = remoteIp4;
			}
			else // AF_INET6
			{
				char remoteIp6[INET6_ADDRSTRLEN];
				auto* client6 = reinterpret_cast<struct sockaddr_in6*>(&client);
				if (ix::inet_ntop(AF_INET6, &client6->sin6_addr, remoteIp6, INET6_ADDRSTRLEN) ==
					nullptr)
				{
					int err = Socket::getErrno();
					std::stringstream ss;
					ss << "error calling inet_ntop (ipv6): " << err << ", " << strerror(err);
					logError(IX_CURRENT_FUNCTION, ss.str());

					Socket::closeSocket(clientFd);

					continue;
				}

				remotePort = ix::network_to_host_short(client6->sin6_port);
				remoteIp = remoteIp6;
			}

			std::shared_ptr<ConnectionState> connectionState;
			if (_connectionStateFactory)
			{
				connectionState = _connectionStateFactory();
			}
			connectionState->setOnSetTerminatedCallback([this] { onSetTerminatedCallback(); });
			connectionState->setRemoteIp(remoteIp);
			connectionState->setRemotePort(remotePort);

			if (_stop)
			{
				Socket::closeSocket(clientFd);
				return;
			}

			// create socket
			std::string errorMsg;
			bool tls = _socketTLSOptions.tls;

			if (_socketTLSOptions.autoDetect)
			{
				// Peek at the first byte to decide: TLS ClientHello starts with 0x16,
				// plain HTTP/WS requests start with an ASCII letter.
				//
				// Polling instead of selecting on purpose: a server that has been running for a while
				// hands out descriptors above FD_SETSIZE, and FD_SET() writes out of bounds for those.
				// Note that the wait happens on the accept thread, so a client that connects without
				// sending anything holds up the connections queued behind it until it times out.
				bool readyToRead = true;
				if (Socket::poll(readyToRead, kTlsAutoDetectTimeoutInMs, clientFd, nullptr) !=
					PollResultType::ReadyForRead)
				{
					logError(IX_CURRENT_FUNCTION, "TLS auto-detect: timeout waiting for first byte");
					Socket::closeSocket(clientFd);
					continue;
				}

				uint8_t firstByte = 0;
				int n = recv(clientFd, (char*) &firstByte, 1, MSG_PEEK);
				if (n <= 0)
				{
					logError(IX_CURRENT_FUNCTION, "TLS auto-detect: failed to peek first byte");
					Socket::closeSocket(clientFd);
					continue;
				}

				tls = (firstByte == 0x16); // 0x16 = TLS record type for ClientHello
			}

			// The descriptor belongs to the socket from here on - it's closed by its destructor, and
			// already closed when none could be created, so it must not be closed here as well
			auto socket = createSocket(tls, clientFd, errorMsg, _socketTLSOptions);

			if (socket == nullptr)
			{
				logError(IX_CURRENT_FUNCTION, "cannot create socket: " + errorMsg);
				continue;
			}

			// Set the socket to non blocking mode + other tweaks
			SocketConnect::configure(clientFd);

			if (!socket->accept(errorMsg))
			{
				logError(IX_CURRENT_FUNCTION, "tls accept failed: " + errorMsg);
				continue;
			}

			// Launch the handleConnection work asynchronously in its own thread.
			std::lock_guard<std::mutex> lock(_connectionsThreadsMutex);
			_connectionsThreads.push_back(std::make_pair(
				connectionState,
				std::thread(
					&SocketServer::handleConnection, this, std::move(socket), connectionState)));
		}
	}

	size_t SocketServer::getConnectionsThreadsCount()
	{
		std::lock_guard<std::mutex> lock(_connectionsThreadsMutex);
		return _connectionsThreads.size();
	}

	void SocketServer::runGC()
	{
		// Use a cryptic name to stay within the 16 bytes limit thread name limitation
		// $ echo Srv:gc:64000 | wc -c
		// 13
		setThreadName("Srv:gc:" + std::to_string(_port));

		for (;;)
		{
			// Garbage collection to shutdown/join threads for closed connections.
			closeTerminatedThreads();

			// We quit this thread if all connections are closed and we received
			// a stop request by setting _stopGc to true.
			if (_stopGc && getConnectionsThreadsCount() == 0)
			{
				break;
			}

			// Unless we are stopping the server, wait for a connection
			// to be terminated to run the threads GC, instead of busy waiting
			// with a sleep
			if (!_stopGc)
			{
				std::unique_lock<std::mutex> lock(_conditionVariableMutexGC);
				if(!_canContinueGC) {
					_conditionVariableGC.wait(lock, [this]{ return _canContinueGC; });
				}
				_canContinueGC = false;
			}
		}
	}

	void SocketServer::setTLSOptions(const SocketTLSOptions& socketTLSOptions)
	{
		_socketTLSOptions = socketTLSOptions;
	}

	void SocketServer::onSetTerminatedCallback()
	{
		// a connection got terminated, we can run the connection thread GC,
		// so wake up the thread responsible for that
		{
			std::lock_guard<std::mutex> lock{ _conditionVariableMutexGC };
			_canContinueGC = true;
		}
		_conditionVariableGC.notify_one();
	}

	int  SocketServer::getPort()
	{
		return _port;
	}

	std::string SocketServer::getHost()
	{
		return _host;
	}

	int SocketServer::getBacklog()
	{
		return _backlog;
	}

	std::size_t SocketServer::getMaxConnections()
	{
		return _maxConnections;
	}

	int SocketServer::getAddressFamily()
	{
		return _addressFamily;
	}
}
