/*
 *  IXSocketConnect.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2018 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include "IXCancellationRequest.h"
#include <string>

struct addrinfo;

namespace ix
{
	/**
		@brief Utility for connecting sockets to remote hosts.
	*/
	class SocketConnect
	{
	public:
		/**
		 * @brief Connect to a remote host.
		 * @param hostname Hostname to connect to.
		 * @param port Port number.
		 * @param errMsg Output error message.
		 * @param isCancellationRequested Cancellation request callback.
		 * @return Socket file descriptor or -1 on error.
		 */
		static int connect(const std::string& hostname,
						   int port,
						   std::string& errMsg,
						   const CancellationRequest& isCancellationRequested);

		/**
		 * @brief Configure socket options after connect.
		 * @param sockfd Socket file descriptor.
		 */
		static void configure(int sockfd);

	private:
		/**
		 * @brief Connect to a specific address (internal).
		 * @param address Address info.
		 * @param errMsg Output error message.
		 * @param isCancellationRequested Cancellation request callback.
		 * @return Socket file descriptor or -1 on error.
		 */
		static int connectToAddress(const struct addrinfo* address,
									std::string& errMsg,
									const CancellationRequest& isCancellationRequested);
	};
}
