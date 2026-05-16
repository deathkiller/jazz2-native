
/*
 *  IXSocketFactory.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2018 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include "IXSocketTLSOptions.h"
#include <memory>
#include <string>

namespace ix
{
	class Socket;

	/**
	 * @brief Factory function for creating @ref Socket objects with optional TLS support.
	 *
	 * Creates a new @ref Socket instance, optionally enabling TLS using the provided @ref SocketTLSOptions.
	 * @param tls Enable TLS if true.
	 * @param fd File descriptor for the socket.
	 * @param errorMsg Output error message.
	 * @param tlsOptions TLS configuration options.
	 * @return Unique pointer to the created @ref Socket.
	 */
	std::unique_ptr<Socket> createSocket(bool tls,
										 int fd,
										 std::string& errorMsg,
										 const SocketTLSOptions& tlsOptions);
}
