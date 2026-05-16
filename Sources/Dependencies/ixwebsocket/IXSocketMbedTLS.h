/*
 *  IXSocketMbedTLS.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2019-2020 Machine Zone, Inc. All rights reserved.
 */
#ifdef IXWEBSOCKET_USE_MBED_TLS

#pragma once

#include "IXSocket.h"
#include "IXSocketTLSOptions.h"
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/debug.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/platform.h>
#include <mbedtls/x509.h>
#include <mbedtls/x509_crt.h>
#include <mutex>

namespace ix
{
	/**
		@brief Secure socket implementation using mbedTLS.
	
		Used by @ref createSocket when mbedTLS is enabled. Handles SSL/TLS handshake and encrypted I/O.
	*/
	class SocketMbedTLS final : public Socket
	{
	public:
		/**
		 * @brief Construct a new SocketMbedTLS.
		 * @param tlsOptions TLS options
		 * @param fd File descriptor (default -1)
		 */
		SocketMbedTLS(const SocketTLSOptions& tlsOptions, int fd = -1);
		/** @brief Destructor. */
		~SocketMbedTLS();

		virtual bool accept(std::string& errMsg) final;

		virtual bool connect(const std::string& host,
					 int port,
					 std::string& errMsg,
					 const CancellationRequest& isCancellationRequested) final;
		virtual void close() final;

		virtual ssize_t send(char* buffer, size_t length) final;
		virtual ssize_t recv(void* buffer, size_t length) final;

	private:
		mbedtls_ssl_context _ssl; /**< mbedTLS SSL context. */
		mbedtls_ssl_config _conf; /**< mbedTLS SSL config. */
		mbedtls_entropy_context _entropy; /**< mbedTLS entropy context. */
		mbedtls_ctr_drbg_context _ctr_drbg; /**< mbedTLS CTR-DRBG context. */
		mbedtls_x509_crt _cacert; /**< mbedTLS CA certificate. */
		mbedtls_x509_crt _cert; /**< mbedTLS certificate. */
		mbedtls_pk_context _pkey; /**< mbedTLS private key. */

		std::mutex _mutex; /**< Thread safety mutex. */
		SocketTLSOptions _tlsOptions; /**< TLS options. */

		bool init(const std::string& host, bool isClient, std::string& errMsg); /**< Initialize mbedTLS. */
		void initMBedTLS(); /**< Initialize mbedTLS library. */
		bool loadSystemCertificates(std::string& errMsg); /**< Load system CA certificates. */
	};

}

#endif // IXWEBSOCKET_USE_MBED_TLS
