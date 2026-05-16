/*
 *  IXSocketTLSOptions.h
 *  Author: Matt DeBoer
 *  Copyright (c) 2019 Machine Zone, Inc. All rights reserved.
 *  Copyright (c) 2026 Dan R.
 */

#pragma once

#include <string>

namespace ix
{
	/**
		@brief TLS options for configuring secure @ref Socket connections.

		Holds certificate, key, CA, and cipher settings for TLS. Used by @ref createSocket and @ref SocketServer.
	*/
	struct SocketTLSOptions
	{
	public:
		/** @brief Check validity of the object. */
		bool isValid() const;

		std::string certFile; /**< Certificate file presented to peers. */
		std::string keyFile; /**< Key file used for signing/encryption. */

		/**
		 * @brief CA certificate (or bundle) file containing certificates to be trusted by peers.
		 * Use `SYSTEM` to leverage system defaults, `NONE` to disable peer verification.
		 */
		std::string caFile = "SYSTEM";

		std::string ciphers = "DEFAULT"; /**< List of ciphers (e.g., rsa, etc.). */
		bool tls = false; /**< Whether TLS is enabled (used for server code). */

		/**
		 * @brief Auto-detect TLS by sniffing the first byte of each incoming connection.
		 * Requires @ref certFile and @ref keyFile to be set.
		 */
		bool autoDetect = false;

		/**
		 * @brief Skip validating the peer's hostname against the certificate presented.
		 */
		bool disable_hostname_validation = false;

		/** @brief Returns true if both cert and key are set. */
		bool hasCertAndKey() const;

		/** @brief Returns true if using system CA defaults. */
		bool isUsingSystemDefaults() const;

		/** @brief Returns true if using in-memory CAs. */
		bool isUsingInMemoryCAs() const;

		/** @brief Returns true if peer verification is disabled. */
		bool isPeerVerifyDisabled() const;

		/** @brief Returns true if using default ciphers. */
		bool isUsingDefaultCiphers() const;

		/** @brief Get the last error message. */
		const std::string& getErrorMsg() const;

		/** @brief Get a description of the TLS options. */
		std::string getDescription() const;

	private:
		mutable std::string _errMsg;
		mutable bool _validated = false;
	};
}
