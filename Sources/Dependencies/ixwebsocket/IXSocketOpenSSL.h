/*
 *  IXSocketOpenSSL.h
 *  Author: Benjamin Sergeant, Matt DeBoer
 *  Copyright (c) 2017-2020 Machine Zone, Inc. All rights reserved.
 */
#if defined(IXWEBSOCKET_USE_OPEN_SSL) || defined(IXWEBSOCKET_USE_LIBRE_SSL)

#pragma once

#include "IXCancellationRequest.h"
#include "IXSocket.h"
#include "IXSocketTLSOptions.h"
#include <mutex>
#include <openssl/bio.h>
#include <openssl/conf.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/ssl.h>

namespace ix
{
	/**
	 * @brief Secure socket implementation using OpenSSL/LibreSSL.
	 *
	 * Used by @ref createSocket when OpenSSL or LibreSSL is enabled. Handles SSL/TLS handshake and encrypted I/O.
	 */
	class SocketOpenSSL final : public Socket
	{
	public:
		/**
		 * @brief Construct a new SocketOpenSSL.
		 * @param tlsOptions TLS options
		 * @param fd File descriptor (default -1)
		 */
		SocketOpenSSL(const SocketTLSOptions& tlsOptions, int fd = -1);
		/** @brief Destructor. */
		~SocketOpenSSL();

		virtual bool accept(std::string& errMsg) final;

		virtual bool connect(const std::string& host,
					 int port,
					 std::string& errMsg,
					 const CancellationRequest& isCancellationRequested) final;
		virtual void close() final;

		virtual ssize_t send(char* buffer, size_t length) final;
		virtual ssize_t recv(void* buffer, size_t length) final;

	private:
		void openSSLInitialize(); /**< Initialize OpenSSL library. */
		std::string getSSLError(int ret); /**< Get OpenSSL error string. */
		SSL_CTX* openSSLCreateContext(std::string& errMsg); /**< Create OpenSSL context. */
		bool openSSLAddCARootsFromString(const std::string roots); /**< Add CA roots from string. */
		bool openSSLClientHandshake(const std::string& hostname,
						std::string& errMsg,
						const CancellationRequest& isCancellationRequested); /**< Perform client handshake. */
		bool openSSLCheckServerCert(SSL* ssl, const std::string& hostname, std::string& errMsg); /**< Check server certificate. */
		bool checkHost(const std::string& host, const char* pattern); /**< Check host name. */
		bool handleTLSOptions(std::string& errMsg); /**< Handle TLS options. */
		bool openSSLServerHandshake(std::string& errMsg); /**< Perform server handshake. */

		// Required for OpenSSL < 1.1
		static void openSSLLockingCallback(int mode, int type, const char* /*file*/, int /*line*/); /**< OpenSSL locking callback. */

		SSL* _ssl_connection; /**< OpenSSL connection. */
		SSL_CTX* _ssl_context; /**< OpenSSL context. */
		const SSL_METHOD* _ssl_method; /**< OpenSSL method. */
		SocketTLSOptions _tlsOptions; /**< TLS options. */

		mutable std::mutex _mutex; /**< OpenSSL routines are not thread-safe. */

		static std::once_flag _openSSLInitFlag;
		static std::atomic<bool> _openSSLInitializationSuccessful;
	};

}

#endif // defined(IXWEBSOCKET_USE_OPEN_SSL) || defined(IXWEBSOCKET_USE_LIBRE_SSL)
