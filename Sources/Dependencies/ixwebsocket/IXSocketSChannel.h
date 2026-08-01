/*
 *  IXSocketSChannel.h
 *  Copyright (c) 2026 Dan R.
 */
#ifdef IXWEBSOCKET_USE_SCHANNEL

#pragma once

#include "IXCancellationRequest.h"
#include "IXNetSystem.h"
#include "IXSocket.h"
#include "IXSocketTLSOptions.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifndef SECURITY_WIN32
#	define SECURITY_WIN32
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
// Exposes `SCH_CREDENTIALS`, the only way to let Schannel negotiate TLS 1.3
#ifndef SCHANNEL_USE_BLACKLISTS
#	define SCHANNEL_USE_BLACKLISTS
#endif

// `IXNetSystem.h` has to be included first, so that <winsock2.h> comes before <windows.h>
#include <windows.h>
#include <winapifamily.h>

// The Schannel, SSPI and CNG headers declare their functions for the desktop and OneCore families
// only, so a Universal Windows Platform build (which compiles as the app family) sees none of them.
// The functions themselves are perfectly usable there - every single one is exported by the
// `WindowsApp` umbrella library that such a build links against - so the family is widened for the
// duration of these includes and put back right afterwards. They must not have been included yet,
// which is why they are pulled in here rather than by whatever includes this header
#if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#	pragma push_macro("WINAPI_FAMILY")
#	undef WINAPI_FAMILY
#	define WINAPI_FAMILY WINAPI_FAMILY_DESKTOP_APP
#	define IXWEBSOCKET_SCHANNEL_WIDENED_FAMILY
#endif

#include <wincrypt.h>
#include <ncrypt.h>
#include <security.h>
#include <subauth.h> // `UNICODE_STRING`, which the credential structures below are built on
#include <schannel.h>

#ifdef IXWEBSOCKET_SCHANNEL_WIDENED_FAMILY
#	undef IXWEBSOCKET_SCHANNEL_WIDENED_FAMILY
#	pragma pop_macro("WINAPI_FAMILY")
#endif

namespace ix
{
	struct SchannelCertificate;

	/**
		@brief Secure socket implementation using Schannel, the Windows-native TLS provider

		Used by @ref createSocket when Schannel is enabled. The TLS handshake and the record
		encryption are performed by the operating system, so neither a third-party TLS library
		has to be present at build time nor an additional runtime library next to the executable.
		This holds for Universal Windows Platform applications as well, where the whole interface
		is reachable through `WindowsApp.lib` (see the includes above).

		Both the client side (@ref connect) and the server side (@ref accept) are supported, the
		latter with the certificate and the private key supplied as PEM files (RSA and EC keys,
		either in the PKCS#1/SEC1 or in the unencrypted PKCS#8 form). Certificate revocation is
		checked only when the system certificate store is used, matching the other TLS backends,
		and @ref SocketTLSOptions::ciphers has no effect, because Schannel takes the enabled
		cipher suites from the system-wide policy instead.
	*/
	class SocketSChannel final : public Socket
	{
	public:
		/**
		 * @brief Construct a new SocketSChannel
		 * @param tlsOptions TLS options
		 * @param fd File descriptor (default -1)
		 */
		SocketSChannel(const SocketTLSOptions& tlsOptions, int fd = -1);
		/** @brief Destructor */
		~SocketSChannel();

		virtual bool accept(std::string& errMsg) final;

		virtual bool connect(const std::string& host,
							 int port,
							 std::string& errMsg,
							 const CancellationRequest& isCancellationRequested) final;
		virtual void close() final;

		virtual ssize_t send(char* buffer, size_t length) final;
		virtual ssize_t recv(void* buffer, size_t length) final;

	private:
		// Creates the Schannel credentials, including the certificate and the trusted roots
		bool acquireCredentials(bool isClient, std::string& errMsg);
		// Loads the certificate chain and the private key of the local peer from the PEM files,
		// reusing the instance that was already loaded for the same pair of files
		std::shared_ptr<SchannelCertificate> loadCertificateAndKey(std::string& errMsg);
		// Loads the certificates to be trusted instead of the ones of the system store
		bool loadTrustedRoots(std::string& errMsg);

		// Exchanges handshake tokens until the secure context is established
		bool performHandshake(std::string& errMsg, const CancellationRequest& isCancellationRequested);
		// Sends a handshake token, waiting for the socket to become writable if needed
		bool sendToken(const void* data, size_t length, std::string& errMsg,
					   const CancellationRequest& isCancellationRequested);
		// Validates the certificate of the peer when Schannel doesn't do it automatically
		bool verifyPeerCertificate(std::string& errMsg);

		// Reads more ciphertext into `_encryptedBuffer`, returning what `::recv` returned
		ssize_t readEncrypted();
		// Whether `recv()` can hand out more data without waiting for the socket
		bool hasPendingInput() const;
		// Writes as much of `_pendingOutput` as the socket accepts, true if all of it went out
		bool flushPendingOutput();
		// Sends the `close_notify` alert to let the peer distinguish a shutdown from a truncation
		void sendCloseNotify();
		// Releases everything but the socket itself, which is left to `Socket::close()`
		void freeSecurityResources();

		CredHandle _credentials; /**< Schannel credentials of the local peer. */
		CtxtHandle _context; /**< Security context of the connection. */
		SecPkgContext_StreamSizes _streamSizes; /**< Header, trailer and payload sizes of a record. */

		bool _hasCredentials; /**< Whether `_credentials` has to be freed. */
		bool _hasContext; /**< Whether `_context` has to be freed. */
		bool _isClient; /**< Whether this side initiated the connection. */
		bool _manualValidation; /**< Whether the peer certificate is validated by this class. */
		bool _peerClosed; /**< Whether the peer has sent its `close_notify` alert. */
		bool _shutdownSent; /**< Whether the `close_notify` alert has already been sent. */

		std::vector<char> _encryptedBuffer; /**< Ciphertext received but not decrypted yet. */
		std::vector<char> _plainBuffer; /**< Decrypted payload not consumed by `recv()` yet. */
		std::size_t _plainOffset; /**< Read position within `_plainBuffer`. */
		std::vector<char> _pendingOutput; /**< Ciphertext the socket didn't accept yet. */
		std::size_t _pendingOffset; /**< Write position within `_pendingOutput`. */
		std::size_t _pendingPlainLength; /**< Payload of the record the caller repeats meanwhile. */

		std::shared_ptr<SchannelCertificate> _certificate; /**< Certificate presented to the peer. */
		HCERTSTORE _caStore; /**< Store holding the explicitly trusted roots. */
		HCERTCHAINENGINE _chainEngine; /**< Chain engine limited to `_caStore`. */

		std::wstring _targetName; /**< Host name of the peer, used for SNI and validation. */
		std::string _host; /**< Host name of the peer, as it was requested. */

		mutable std::mutex _mutex; /**< A security context cannot be used concurrently. */

		SocketTLSOptions _tlsOptions; /**< TLS options. */
	};
}

#endif // IXWEBSOCKET_USE_SCHANNEL
