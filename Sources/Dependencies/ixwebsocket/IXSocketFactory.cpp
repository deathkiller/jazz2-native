/*
 *  IXSocketFactory.cpp
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2019 Machine Zone, Inc. All rights reserved.
 */

#include "IXSocketFactory.h"

#include "IXUniquePtr.h"
#ifdef IXWEBSOCKET_USE_TLS

#ifdef IXWEBSOCKET_USE_MBED_TLS
#include "IXSocketMbedTLS.h"
#elif defined(IXWEBSOCKET_USE_SCHANNEL)
#include "IXSocketSChannel.h"
#elif defined(IXWEBSOCKET_USE_OPEN_SSL) || defined(IXWEBSOCKET_USE_LIBRE_SSL)
#include "IXSocketOpenSSL.h"
#elif __APPLE__
#include "IXSocketAppleSSL.h"
#endif

#else

#include "IXSocket.h"

#endif

namespace ix
{
	std::unique_ptr<Socket> createSocket(bool tls,
										 int fd,
										 std::string& errorMsg,
										 const SocketTLSOptions& tlsOptions)
	{
		(void) tlsOptions;
		errorMsg.clear();
		std::unique_ptr<Socket> socket;

		if (!tls)
		{
			socket = ix::make_unique<Socket>(fd);
		}
		else
		{
#ifdef IXWEBSOCKET_USE_TLS
#if defined(IXWEBSOCKET_USE_MBED_TLS)
			socket = ix::make_unique<SocketMbedTLS>(tlsOptions, fd);
#elif defined(IXWEBSOCKET_USE_SCHANNEL)
			socket = ix::make_unique<SocketSChannel>(tlsOptions, fd);
#elif defined(IXWEBSOCKET_USE_OPEN_SSL) || defined(IXWEBSOCKET_USE_LIBRE_SSL)
			socket = ix::make_unique<SocketOpenSSL>(tlsOptions, fd);
#elif defined(__APPLE__)
			socket = ix::make_unique<SocketAppleSSL>(tlsOptions, fd);
#endif
#else
			errorMsg = "TLS support is not enabled on this platform.";

			// The descriptor is taken over even when no socket can be created, so that callers have one
			// rule to follow - on failure it is closed already, and closing it again could take down an
			// unrelated connection that has been given the same number in the meantime
			if (fd != -1)
			{
				Socket::closeSocket(fd);
			}
			return nullptr;
#endif
		}

		// The socket owns the descriptor from here on, its destructor closes it
		if (!socket->init(errorMsg))
		{
			socket.reset();
		}

		return socket;
	}
}
