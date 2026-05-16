/*
 *  IXUdpSocket.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2020 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include <atomic>
#include <memory>
#include <string>

#ifdef _WIN32
#include <basetsd.h>
#ifdef _MSC_VER
typedef SSIZE_T ssize_t;
#endif
#endif

#include "IXNetSystem.h"

namespace ix
{
	/**
		@brief UDP socket abstraction for datagram communication.

		Provides methods for sending and receiving UDP packets. Used for connectionless network communication.
	*/
	class UdpSocket
	{
	public:
		/**
		 * @brief Construct a new UdpSocket.
		 * @param fd File descriptor (default -1).
		 */
		UdpSocket(int fd = -1);
		/** @brief Destructor. */
		~UdpSocket();

		/**
		 * @brief Initialize the UDP socket.
		 * @param host Host address.
		 * @param port Port number.
		 * @param errMsg Error message output.
		 * @return True on success.
		 */
		bool init(const std::string& host, int port, std::string& errMsg);
		/**
		 * @brief Send data to the remote host.
		 * @param buffer Data to send.
		 * @return Number of bytes sent.
		 */
		ssize_t sendto(const std::string& buffer);
		/**
		 * @brief Receive data from the remote host.
		 * @param buffer Buffer to store received data.
		 * @param length Length of the buffer.
		 * @return Number of bytes received.
		 */
		ssize_t recvfrom(char* buffer, size_t length);

		/** @brief Close the UDP socket. */
		void close();

		/** @brief Get the last error number. */
		static int getErrno();
		/** @brief Returns true if waiting is needed. */
		static bool isWaitNeeded();
		/** @brief Close a socket by file descriptor. */
		static void closeSocket(int fd);

	private:
		/** @brief Socket file descriptor. */
		std::atomic<int> _sockfd;
		/** @brief Server address structure. */
		struct sockaddr_in _server;
	};
}
