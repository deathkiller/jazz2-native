/*
 *  IXSocket.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2017-2018 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#ifdef __APPLE__
#include <sys/types.h>
#endif

#ifdef _WIN32
#include <basetsd.h>
#ifdef _MSC_VER
typedef SSIZE_T ssize_t;
#endif
#endif

#include "IXCancellationRequest.h"
#include "IXProgressCallback.h"
#include "IXSelectInterrupt.h"

namespace ix
{
	/**
		@brief Result type for socket polling operations.
	*/
	enum class PollResultType
	{
		ReadyForRead = 0,    /**< Socket is ready for reading. */
		ReadyForWrite = 1,   /**< Socket is ready for writing. */
		Timeout = 2,         /**< Poll timed out. */
		Error = 3,           /**< Poll error occurred. */
		SendRequest = 4,     /**< Send request event. */
		CloseRequest = 5     /**< Close request event. */
	};

	/**
		@brief TCP socket abstraction for client and server operations.
	
		Provides methods for polling, reading, writing, and managing socket state.
		Used by @ref SocketServer and related networking classes.
	*/
	class Socket
	{
	public:
		/**
		 * @brief Construct a new Socket.
		 * @param fd File descriptor (default -1).
		 */
		Socket(int fd = -1);
		/** @brief Destructor. */
		virtual ~Socket();
		/**
		 * @brief Initialize the socket.
		 * @param errorMsg Error message output.
		 * @return True on success.
		 */
		bool init(std::string& errorMsg);

		/**
		 * @brief Poll the socket for activity.
		 * @param timeoutMs Timeout in milliseconds (default: `kDefaultPollTimeout`).
		 * @return @ref PollResultType
		 */
		PollResultType poll(int timeoutMs = kDefaultPollTimeout);
		/**
		 * @brief Wake up the socket from poll.
		 * @param wakeUpCode Special code (see @ref SelectInterrupt).
		 * @return True if successful.
		 */
		bool wakeUpFromPoll(uint64_t wakeUpCode);
		/** @brief Check if wake up from poll is supported. */
		bool isWakeUpFromPollSupported();

		/**
		 * @brief Check if socket is ready to write.
		 * @param timeoutMs Timeout in milliseconds.
		 * @return @ref PollResultType
		 */
		PollResultType isReadyToWrite(int timeoutMs);
		/**
		 * @brief Check if socket is ready to read.
		 * @param timeoutMs Timeout in milliseconds.
		 * @return @ref PollResultType
		 */
		PollResultType isReadyToRead(int timeoutMs);

		/**
		 * @brief Accept a new connection (virtual, override in derived classes).
		 * @param errMsg Error message output.
		 * @return True on success.
		 */
		virtual bool accept(std::string& errMsg);

		virtual bool connect(const std::string& host,
							 int port,
							 std::string& errMsg,
							 const CancellationRequest& isCancellationRequested);
		virtual void close();

		virtual ssize_t send(char* buffer, size_t length);
		ssize_t send(const std::string& buffer);
		virtual ssize_t recv(void* buffer, size_t length);

		// Blocking and cancellable versions, working with socket that can be set
		// to non blocking mode. Used during HTTP upgrade.
		bool readByte(void* buffer, const CancellationRequest& isCancellationRequested);
		bool writeBytes(const std::string& str, const CancellationRequest& isCancellationRequested);

		std::pair<bool, std::string> readLine(const CancellationRequest& isCancellationRequested);
		std::pair<bool, std::string> readBytes(size_t length,
											   const OnProgressCallback& onProgressCallback,
											   const OnChunkCallback& onChunkCallback,
											   const CancellationRequest& isCancellationRequested);

		static int getErrno();
		static bool isWaitNeeded();
		static void closeSocket(int fd);

		static PollResultType poll(bool readyToRead,
								   int timeoutMs,
								   int sockfd,
								   const SelectInterruptPtr& selectInterrupt);

	protected:
		std::atomic<int> _sockfd;
		std::mutex _socketMutex;

		static bool readSelectInterruptRequest(const SelectInterruptPtr& selectInterrupt,
											   PollResultType* pollResult);

	private:
		static const int kDefaultPollTimeout;
		static const int kDefaultPollNoTimeout;

		SelectInterruptPtr _selectInterrupt;
	};
}
