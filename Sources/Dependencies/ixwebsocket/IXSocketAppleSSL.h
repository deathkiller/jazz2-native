/*
 *  IXSocketAppleSSL.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2017-2020 Machine Zone, Inc. All rights reserved.
 */
#ifdef IXWEBSOCKET_USE_SECURE_TRANSPORT

#pragma once

#include "IXCancellationRequest.h"
#include "IXSocket.h"
#include "IXSocketTLSOptions.h"
#include <Security/SecureTransport.h>
#include <Security/Security.h>
#include <mutex>

namespace ix
{
	/**
	 * @brief Secure socket implementation using Apple Secure Transport (macOS/iOS).
	 *
	 * Used by @ref createSocket when Secure Transport is enabled. Handles SSL/TLS handshake and encrypted I/O.
	 */
	class SocketAppleSSL final : public Socket
	{
	public:
		/**
		 * @brief Construct a new SocketAppleSSL.
		 * @param tlsOptions TLS options
		 * @param fd File descriptor (default -1)
		 */
		SocketAppleSSL(const SocketTLSOptions& tlsOptions, int fd = -1);
		/** @brief Destructor. */
		~SocketAppleSSL();

		virtual bool accept(std::string& errMsg) final;

		virtual bool connect(const std::string& host,
					 int port,
					 std::string& errMsg,
					 const CancellationRequest& isCancellationRequested) final;
		virtual void close() final;

		virtual ssize_t send(char* buffer, size_t length) final;
		virtual ssize_t recv(void* buffer, size_t length) final;

	private:
		static std::string getSSLErrorDescription(OSStatus status); /**< Get error description for OSStatus. */
		static OSStatus writeToSocket(SSLConnectionRef connection, const void* data, size_t* len); /**< Write to socket callback. */
		static OSStatus readFromSocket(SSLConnectionRef connection, void* data, size_t* len); /**< Read from socket callback. */

		OSStatus tlsHandShake(std::string& errMsg,
					  const CancellationRequest& isCancellationRequested); /**< Perform TLS handshake. */

		SSLContextRef _sslContext; /**< Apple Secure Transport context. */
		mutable std::mutex _mutex; /**< AppleSSL routines are not thread-safe. */

		SocketTLSOptions _tlsOptions; /**< TLS options. */
	};

}

#endif // IXWEBSOCKET_USE_SECURE_TRANSPORT
