/*
 *  IXSocketSChannel.cpp
 *  Copyright (c) 2026 Dan R.
 *
 *  TLS backend built on Schannel, the security support provider that ships with Windows, so that
 *  no third-party TLS library is needed. The peers are driven with a non-blocking socket, which
 *  is why the ciphertext of both directions has to be buffered here.
 */
#ifdef IXWEBSOCKET_USE_SCHANNEL

#include "IXSocketSChannel.h"

#include "IXSocketConnect.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

namespace ix
{
	namespace
	{
		// A TLS record cannot be larger than this, so a message is always complete after that many bytes
		constexpr std::size_t kMaxRecordSize = 16384 + 512;
		// The whole handshake is expected to fit in a couple of records
		constexpr std::size_t kMaxHandshakeSize = 128 * 1024;
		// How long a handshake may take in total, so that a stuck peer cannot block the accept loop
		constexpr int kHandshakeTimeoutMs = 30000;
		// How long a single wait for the socket to become readable or writable may take
		constexpr int kHandshakePollTimeoutMs = 50;
		// `SECURITY_FLAG_IGNORE_CERT_CN_INVALID` of <wininet.h>, which is not worth including here
		constexpr DWORD kIgnoreCertificateNameMismatch = 0x00001000;
		// Wakes up a poll that waits on the socket while decrypted data is already buffered here.
		// Anything that is neither `kSendRequest` nor `kCloseRequest` makes @ref Socket::poll
		// report @ref PollResultType::ReadyForRead, which is exactly what such a poll has to do
		constexpr std::uint64_t kBufferedDataRequest = 3;
		// Size of the header of a TLS record: the content type, the version and the payload length
		constexpr std::size_t kRecordHeaderSize = 5;

		std::wstring toWideString(const std::string& value)
		{
			if (value.empty())
			{
				return std::wstring();
			}

			int length = ::MultiByteToWideChar(
				CP_UTF8, 0, value.c_str(), (int) value.size(), nullptr, 0);
			if (length <= 0)
			{
				return std::wstring();
			}

			std::wstring result((std::size_t) length, L'\0');
			::MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int) value.size(), &result[0], length);
			return result;
		}

		std::string describeStatus(SECURITY_STATUS status)
		{
			char* buffer = nullptr;
			DWORD length = ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
												FORMAT_MESSAGE_FROM_SYSTEM |
												FORMAT_MESSAGE_IGNORE_INSERTS,
											nullptr,
											(DWORD) status,
											0,
											(LPSTR) &buffer,
											0,
											nullptr);

			std::string message;
			if (length > 0 && buffer != nullptr)
			{
				while (length > 0 && (buffer[length - 1] == '\r' || buffer[length - 1] == '\n' ||
									  buffer[length - 1] == ' ' || buffer[length - 1] == '.'))
				{
					--length;
				}
				message.assign(buffer, length);
			}
			if (buffer != nullptr)
			{
				::LocalFree(buffer);
			}

			std::stringstream ss;
			if (!message.empty())
			{
				ss << message << " ";
			}
			ss << "(0x" << std::hex << (unsigned long) status << ")";
			return ss.str();
		}

		bool readEntireFile(const std::string& path, std::string& contents)
		{
			std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
			if (!file)
			{
				return false;
			}

			std::stringstream ss;
			ss << file.rdbuf();
			contents = ss.str();
			return true;
		}

		// A single `-----BEGIN <label>-----` block of a PEM file, already Base64 decoded
		struct PemBlock
		{
			std::string label;
			std::vector<BYTE> data;
		};

		std::vector<PemBlock> decodePem(const std::string& text)
		{
			const std::string beginMarker = "-----BEGIN ";
			const std::string endMarker = "-----END ";

			std::vector<PemBlock> blocks;
			std::size_t position = 0;

			while (true)
			{
				std::size_t begin = text.find(beginMarker, position);
				if (begin == std::string::npos)
				{
					break;
				}

				std::size_t labelEnd = text.find("-----", begin + beginMarker.size());
				std::size_t end = text.find(endMarker, begin);
				if (labelEnd == std::string::npos || end == std::string::npos)
				{
					break;
				}

				std::size_t blockEnd = text.find("-----", end + endMarker.size());
				blockEnd = (blockEnd == std::string::npos) ? text.size() : blockEnd + 5;
				position = blockEnd;

				PemBlock block;
				block.label = text.substr(begin + beginMarker.size(),
										  labelEnd - begin - beginMarker.size());

				const char* source = text.c_str() + begin;
				DWORD sourceLength = (DWORD)(blockEnd - begin);
				DWORD binaryLength = 0;
				if (!::CryptStringToBinaryA(source, sourceLength, CRYPT_STRING_BASE64HEADER, nullptr,
											&binaryLength, nullptr, nullptr) ||
					binaryLength == 0)
				{
					continue;
				}

				block.data.resize(binaryLength);
				if (!::CryptStringToBinaryA(source, sourceLength, CRYPT_STRING_BASE64HEADER,
											&block.data[0], &binaryLength, nullptr, nullptr))
				{
					continue;
				}

				block.data.resize(binaryLength);
				blocks.push_back(std::move(block));
			}

			return blocks;
		}

		// Wraps a bare PKCS#1 or SEC1 key in the PKCS#8 envelope that CNG can import directly
		bool wrapInPrivateKeyInfo(const char* algorithmOid,
								  const CRYPT_OBJID_BLOB& parameters,
								  const BYTE* key,
								  DWORD keyLength,
								  std::vector<BYTE>& result)
		{
			CRYPT_PRIVATE_KEY_INFO info;
			std::memset(&info, 0, sizeof(info));
			info.Version = 0;
			info.Algorithm.pszObjId = (LPSTR) algorithmOid;
			info.Algorithm.Parameters = parameters;
			info.PrivateKey.cbData = keyLength;
			info.PrivateKey.pbData = (BYTE*) key;

			DWORD encodedLength = 0;
			if (!::CryptEncodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
									   PKCS_PRIVATE_KEY_INFO, &info, 0, nullptr, nullptr,
									   &encodedLength))
			{
				return false;
			}

			result.resize(encodedLength);
			return ::CryptEncodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
										 PKCS_PRIVATE_KEY_INFO, &info, 0, nullptr, &result[0],
										 &encodedLength) != FALSE;
		}

		bool importPkcs8PrivateKey(NCRYPT_PROV_HANDLE provider,
								   const std::wstring& keyName,
								   const BYTE* key,
								   DWORD keyLength,
								   NCRYPT_KEY_HANDLE& handle,
								   std::string& errMsg)
		{
			// Schannel resolves the private key of a certificate through its key container, an
			// ephemeral key handle is refused, so the key has to be given a name
			NCryptBuffer nameBuffer;
			std::memset(&nameBuffer, 0, sizeof(nameBuffer));
			nameBuffer.BufferType = NCRYPTBUFFER_PKCS_KEY_NAME;
			nameBuffer.cbBuffer = (ULONG)((keyName.size() + 1) * sizeof(wchar_t));
			nameBuffer.pvBuffer = (PVOID) keyName.c_str();

			NCryptBufferDesc parameters;
			std::memset(&parameters, 0, sizeof(parameters));
			parameters.ulVersion = NCRYPTBUFFER_VERSION;
			parameters.cBuffers = 1;
			parameters.pBuffers = &nameBuffer;

			SECURITY_STATUS status = ::NCryptImportKey(provider, 0, NCRYPT_PKCS8_PRIVATE_KEY_BLOB,
													   &parameters, &handle, (PBYTE) key, keyLength,
													   NCRYPT_OVERWRITE_KEY_FLAG |
														   NCRYPT_SILENT_FLAG);
			if (status != ERROR_SUCCESS)
			{
				errMsg = "Cannot import the private key: " + describeStatus(status);
				return false;
			}

			return true;
		}

		bool importRsaPrivateKey(NCRYPT_PROV_HANDLE provider,
								 const std::wstring& keyName,
								 const BYTE* key,
								 DWORD keyLength,
								 NCRYPT_KEY_HANDLE& handle,
								 std::string& errMsg)
		{
			// The parameters of an RSA algorithm identifier are an ASN.1 NULL
			BYTE null[2] = {0x05, 0x00};
			CRYPT_OBJID_BLOB parameters;
			parameters.cbData = sizeof(null);
			parameters.pbData = null;

			std::vector<BYTE> wrapped;
			if (!wrapInPrivateKeyInfo(szOID_RSA_RSA, parameters, key, keyLength, wrapped))
			{
				errMsg = "Cannot wrap the RSA private key: " +
						 describeStatus((SECURITY_STATUS)::GetLastError());
				return false;
			}

			return importPkcs8PrivateKey(provider, keyName, &wrapped[0], (DWORD) wrapped.size(),
										 handle, errMsg);
		}

		bool importEccPrivateKey(NCRYPT_PROV_HANDLE provider,
								 const std::wstring& keyName,
								 const BYTE* key,
								 DWORD keyLength,
								 NCRYPT_KEY_HANDLE& handle,
								 std::string& errMsg)
		{
			// The curve is part of the SEC1 structure, but PKCS#8 wants it in the algorithm
			// identifier, so it has to be decoded and encoded again
			CRYPT_ECC_PRIVATE_KEY_INFO* info = nullptr;
			DWORD infoLength = 0;
			if (!::CryptDecodeObjectEx(X509_ASN_ENCODING, X509_ECC_PRIVATE_KEY, key, keyLength,
									   CRYPT_DECODE_ALLOC_FLAG, nullptr, &info, &infoLength))
			{
				errMsg = "Cannot decode the EC private key: " +
						 describeStatus((SECURITY_STATUS)::GetLastError());
				return false;
			}

			if (info->szCurveOid == nullptr)
			{
				::LocalFree(info);
				errMsg = "Cannot import an EC private key without a named curve";
				return false;
			}

			BYTE* curve = nullptr;
			DWORD curveLength = 0;
			bool encoded = ::CryptEncodeObjectEx(X509_ASN_ENCODING, X509_OBJECT_IDENTIFIER,
												 &info->szCurveOid, CRYPT_ENCODE_ALLOC_FLAG, nullptr,
												 &curve, &curveLength) != FALSE;

			// The curve belongs to the algorithm identifier of the envelope only, so the copy that
			// goes inside of it is encoded again without it
			BYTE* stripped = nullptr;
			DWORD strippedLength = 0;
			if (encoded)
			{
				CRYPT_ECC_PRIVATE_KEY_INFO copy = *info;
				copy.szCurveOid = nullptr;
				if (!::CryptEncodeObjectEx(X509_ASN_ENCODING, X509_ECC_PRIVATE_KEY, &copy,
										   CRYPT_ENCODE_ALLOC_FLAG, nullptr, &stripped,
										   &strippedLength))
				{
					stripped = nullptr;
				}
			}

			::LocalFree(info);

			if (!encoded)
			{
				errMsg = "Cannot encode the curve of the EC private key: " +
						 describeStatus((SECURITY_STATUS)::GetLastError());
				return false;
			}

			CRYPT_OBJID_BLOB parameters;
			parameters.cbData = curveLength;
			parameters.pbData = curve;

			std::vector<BYTE> wrapped;
			bool wrappedOk = wrapInPrivateKeyInfo(szOID_ECC_PUBLIC_KEY, parameters,
												  (stripped != nullptr) ? stripped : key,
												  (stripped != nullptr) ? strippedLength : keyLength,
												  wrapped);
			::LocalFree(curve);
			if (stripped != nullptr)
			{
				::LocalFree(stripped);
			}

			if (!wrappedOk)
			{
				errMsg = "Cannot wrap the EC private key: " +
						 describeStatus((SECURITY_STATUS)::GetLastError());
				return false;
			}

			return importPkcs8PrivateKey(provider, keyName, &wrapped[0], (DWORD) wrapped.size(),
										 handle, errMsg);
		}

		bool importPrivateKey(NCRYPT_PROV_HANDLE provider,
							  const std::wstring& keyName,
							  const PemBlock& block,
							  NCRYPT_KEY_HANDLE& handle,
							  std::string& errMsg)
		{
			if (block.data.empty())
			{
				errMsg = "The private key is empty";
				return false;
			}

			const BYTE* key = &block.data[0];
			DWORD keyLength = (DWORD) block.data.size();

			if (block.label == "PRIVATE KEY")
			{
				return importPkcs8PrivateKey(provider, keyName, key, keyLength, handle, errMsg);
			}
			if (block.label == "RSA PRIVATE KEY")
			{
				return importRsaPrivateKey(provider, keyName, key, keyLength, handle, errMsg);
			}
			if (block.label == "EC PRIVATE KEY")
			{
				return importEccPrivateKey(provider, keyName, key, keyLength, handle, errMsg);
			}
			if (block.label == "ENCRYPTED PRIVATE KEY")
			{
				errMsg = "Encrypted private keys are not supported, decrypt the key file first";
				return false;
			}

			errMsg = "Unsupported private key format \"" + block.label + "\"";
			return false;
		}

		// Names the key container after the thumbprint of the certificate, so that a key left
		// behind by a run that didn't get to clean up is reused instead of piling up
		std::wstring keyContainerName(PCCERT_CONTEXT context)
		{
			std::wstring name = L"IXWebSocket-";

			BYTE thumbprint[20];
			DWORD thumbprintLength = sizeof(thumbprint);
			if (::CertGetCertificateContextProperty(context, CERT_HASH_PROP_ID, thumbprint,
													&thumbprintLength))
			{
				const wchar_t* digits = L"0123456789abcdef";
				for (DWORD i = 0; i < thumbprintLength; i++)
				{
					name += digits[thumbprint[i] >> 4];
					name += digits[thumbprint[i] & 0x0F];
				}
			}

			return name;
		}
	}

	/**
		@brief Certificate and private key that a @ref SocketSChannel presents to its peer

		Loading these is expensive and has a side effect, because the private key of the PEM file
		has to be written to the key storage of the user to be usable by Schannel. Every socket of
		a process therefore shares one instance per pair of files, which also keeps the connections
		from overwriting or deleting each other's key container.
	*/
	struct SchannelCertificate
	{
		SchannelCertificate()
			: context(nullptr)
			, store(nullptr)
			, provider(0)
			, key(0)
		{
		}

		~SchannelCertificate()
		{
			if (context != nullptr)
			{
				::CertFreeCertificateContext(context);
			}
			if (store != nullptr)
			{
				::CertCloseStore(store, 0);
			}
			if (key != 0)
			{
				// This also releases the handle, so it must not be freed again
				::NCryptDeleteKey(key, NCRYPT_SILENT_FLAG);
			}
			if (provider != 0)
			{
				::NCryptFreeObject(provider);
			}
		}

		PCCERT_CONTEXT context; /**< Certificate presented to the peer. */
		HCERTSTORE store; /**< Store holding the certificate and its issuers. */
		NCRYPT_PROV_HANDLE provider; /**< Provider the private key was imported into. */
		NCRYPT_KEY_HANDLE key; /**< Private key belonging to the certificate. */
		std::wstring keyName; /**< Name of the key container of the private key. */
	};

	SocketSChannel::SocketSChannel(const SocketTLSOptions& tlsOptions, int fd)
		: Socket(fd)
		, _hasCredentials(false)
		, _hasContext(false)
		, _isClient(true)
		, _manualValidation(false)
		, _peerClosed(false)
		, _shutdownSent(false)
		, _plainOffset(0)
		, _pendingOffset(0)
		, _pendingPlainLength(0)
		, _caStore(nullptr)
		, _chainEngine(nullptr)
		, _tlsOptions(tlsOptions)
	{
		SecInvalidateHandle(&_credentials);
		SecInvalidateHandle(&_context);
		std::memset(&_streamSizes, 0, sizeof(_streamSizes));
	}

	SocketSChannel::~SocketSChannel()
	{
		SocketSChannel::close();
	}

	std::shared_ptr<SchannelCertificate> SocketSChannel::loadCertificateAndKey(std::string& errMsg)
	{
		static std::mutex certificatesMutex;
		static std::map<std::string, std::shared_ptr<SchannelCertificate>> certificates;

		std::string cacheKey = _tlsOptions.certFile + "\n" + _tlsOptions.keyFile;
		std::lock_guard<std::mutex> lock(certificatesMutex);

		auto cached = certificates.find(cacheKey);
		if (cached != certificates.end())
		{
			return cached->second;
		}

		std::string contents;
		if (!readEntireFile(_tlsOptions.certFile, contents))
		{
			errMsg = "Cannot read the certificate file \"" + _tlsOptions.certFile + "\"";
			return nullptr;
		}

		auto certificate = std::make_shared<SchannelCertificate>();

		// The whole chain goes into a store of its own, so that Schannel can find the issuers of
		// the certificate that is presented to the peer
		certificate->store = ::CertOpenStore(CERT_STORE_PROV_MEMORY, X509_ASN_ENCODING, 0,
											 CERT_STORE_CREATE_NEW_FLAG, nullptr);
		if (certificate->store == nullptr)
		{
			errMsg = "Cannot create the certificate store: " +
					 describeStatus((SECURITY_STATUS)::GetLastError());
			return nullptr;
		}

		for (const PemBlock& block : decodePem(contents))
		{
			if (block.label.find("CERTIFICATE") == std::string::npos || block.data.empty())
			{
				continue;
			}

			PCCERT_CONTEXT context = nullptr;
			if (!::CertAddEncodedCertificateToStore(certificate->store, X509_ASN_ENCODING,
													&block.data[0], (DWORD) block.data.size(),
													CERT_STORE_ADD_ALWAYS, &context))
			{
				errMsg = "Cannot add a certificate of \"" + _tlsOptions.certFile +
						 "\" to the store: " + describeStatus((SECURITY_STATUS)::GetLastError());
				return nullptr;
			}

			// The first certificate of the file is the one belonging to the private key
			if (certificate->context == nullptr)
			{
				certificate->context = context;
			}
			else
			{
				::CertFreeCertificateContext(context);
			}
		}

		if (certificate->context == nullptr)
		{
			errMsg = "No certificate found in \"" + _tlsOptions.certFile + "\"";
			return nullptr;
		}

		if (!readEntireFile(_tlsOptions.keyFile, contents))
		{
			errMsg = "Cannot read the key file \"" + _tlsOptions.keyFile + "\"";
			return nullptr;
		}

		const PemBlock* keyBlock = nullptr;
		std::vector<PemBlock> keyBlocks = decodePem(contents);
		for (const PemBlock& block : keyBlocks)
		{
			if (block.label.find("PRIVATE KEY") != std::string::npos)
			{
				keyBlock = &block;
				break;
			}
		}

		if (keyBlock == nullptr)
		{
			errMsg = "No private key found in \"" + _tlsOptions.keyFile + "\"";
			return nullptr;
		}

		SECURITY_STATUS status =
			::NCryptOpenStorageProvider(&certificate->provider, MS_KEY_STORAGE_PROVIDER, 0);
		if (status != ERROR_SUCCESS)
		{
			errMsg = "Cannot open the key storage provider: " + describeStatus(status);
			return nullptr;
		}

		certificate->keyName = keyContainerName(certificate->context);
		if (!importPrivateKey(certificate->provider, certificate->keyName, *keyBlock,
							  certificate->key, errMsg))
		{
			errMsg += " (\"" + _tlsOptions.keyFile + "\")";
			return nullptr;
		}

		CRYPT_KEY_PROV_INFO keyProviderInfo;
		std::memset(&keyProviderInfo, 0, sizeof(keyProviderInfo));
		keyProviderInfo.pwszContainerName = &certificate->keyName[0];
		keyProviderInfo.pwszProvName = (LPWSTR) MS_KEY_STORAGE_PROVIDER;

		if (!::CertSetCertificateContextProperty(certificate->context, CERT_KEY_PROV_INFO_PROP_ID, 0,
												 &keyProviderInfo))
		{
			errMsg = "Cannot associate the private key with the certificate: " +
					 describeStatus((SECURITY_STATUS)::GetLastError());
			return nullptr;
		}

		certificates[cacheKey] = certificate;
		return certificate;
	}

	bool SocketSChannel::loadTrustedRoots(std::string& errMsg)
	{
		std::string certificates;
		if (_tlsOptions.isUsingInMemoryCAs())
		{
			certificates = _tlsOptions.caFile;
		}
		else if (!readEntireFile(_tlsOptions.caFile, certificates))
		{
			errMsg = "Cannot read the CA file \"" + _tlsOptions.caFile + "\"";
			return false;
		}

		_caStore = ::CertOpenStore(CERT_STORE_PROV_MEMORY, X509_ASN_ENCODING, 0,
								   CERT_STORE_CREATE_NEW_FLAG, nullptr);
		if (_caStore == nullptr)
		{
			errMsg = "Cannot create the CA store: " +
					 describeStatus((SECURITY_STATUS)::GetLastError());
			return false;
		}

		int count = 0;
		std::vector<PemBlock> blocks = decodePem(certificates);
		if (blocks.empty() && !certificates.empty())
		{
			// Not a PEM file, so it can only be a single DER encoded certificate
			if (::CertAddEncodedCertificateToStore(_caStore, X509_ASN_ENCODING,
												   (const BYTE*) certificates.data(),
												   (DWORD) certificates.size(),
												   CERT_STORE_ADD_ALWAYS, nullptr))
			{
				count++;
			}
		}

		for (const PemBlock& block : blocks)
		{
			if (block.label.find("CERTIFICATE") == std::string::npos || block.data.empty())
			{
				continue;
			}

			if (::CertAddEncodedCertificateToStore(_caStore, X509_ASN_ENCODING, &block.data[0],
												   (DWORD) block.data.size(), CERT_STORE_ADD_ALWAYS,
												   nullptr))
			{
				count++;
			}
		}

		if (count == 0)
		{
			errMsg = "No certificate found in \"" + _tlsOptions.caFile + "\"";
			return false;
		}

		// Chains are built against these roots only, the ones of the system are not consulted
		CERT_CHAIN_ENGINE_CONFIG config;
		std::memset(&config, 0, sizeof(config));
		config.cbSize = sizeof(config);
		config.hExclusiveRoot = _caStore;
		config.dwExclusiveFlags = CERT_CHAIN_EXCLUSIVE_ENABLE_CA_FLAG;

		if (!::CertCreateCertificateChainEngine(&config, &_chainEngine))
		{
			errMsg = "Cannot create the certificate chain engine: " +
					 describeStatus((SECURITY_STATUS)::GetLastError());
			return false;
		}

		return true;
	}

	bool SocketSChannel::acquireCredentials(bool isClient, std::string& errMsg)
	{
		if (_tlsOptions.hasCertAndKey())
		{
			_certificate = loadCertificateAndKey(errMsg);
			if (_certificate == nullptr)
			{
				return false;
			}
		}

		// Schannel validates the peer itself only against the certificate store of the system,
		// everything else has to be checked by `verifyPeerCertificate()`
		_manualValidation = !isClient || _tlsOptions.isPeerVerifyDisabled() ||
							!_tlsOptions.isUsingSystemDefaults();

		if (!_tlsOptions.isPeerVerifyDisabled() && !_tlsOptions.isUsingSystemDefaults() &&
			!loadTrustedRoots(errMsg))
		{
			return false;
		}

		DWORD flags = SCH_USE_STRONG_CRYPTO;
		if (isClient)
		{
			flags |= SCH_CRED_NO_DEFAULT_CREDS;
			if (_manualValidation)
			{
				flags |= SCH_CRED_MANUAL_CRED_VALIDATION;
			}
			else
			{
				flags |= SCH_CRED_AUTO_CRED_VALIDATION |
						 SCH_CRED_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
			}
			if (_tlsOptions.disable_hostname_validation)
			{
				flags |= SCH_CRED_NO_SERVERNAME_CHECK;
			}
		}
		else
		{
			flags |= SCH_CRED_NO_SYSTEM_MAPPER | SCH_CRED_MANUAL_CRED_VALIDATION;
		}

		PCCERT_CONTEXT certificates[1] = {(_certificate != nullptr) ? _certificate->context
																   : nullptr};
		DWORD certificateCount = (certificates[0] != nullptr) ? 1 : 0;
		unsigned long usage = isClient ? SECPKG_CRED_OUTBOUND : SECPKG_CRED_INBOUND;
		SECURITY_STATUS status = SEC_E_INTERNAL_ERROR;

		// `SCH_CRED_MAX_SUPPORTED_PARAMETERS` comes with the structures that `SCHANNEL_USE_BLACKLISTS`
		// exposes, so it tells whether the Windows SDK in use knows about them at all
#ifdef SCH_CRED_MAX_SUPPORTED_PARAMETERS
		// TLS 1.3 is only reachable through the newer structure, which needs Windows 10 1809
		TLS_PARAMETERS tlsParameters;
		std::memset(&tlsParameters, 0, sizeof(tlsParameters));
		tlsParameters.grbitDisabledProtocols =
			SP_PROT_SSL2 | SP_PROT_SSL3 | SP_PROT_TLS1_0 | SP_PROT_TLS1_1;

		SCH_CREDENTIALS credentials;
		std::memset(&credentials, 0, sizeof(credentials));
		credentials.dwVersion = SCH_CREDENTIALS_VERSION;
		credentials.dwFlags = flags;
		credentials.cCreds = certificateCount;
		credentials.paCred = (certificateCount > 0) ? certificates : nullptr;
		credentials.cTlsParameters = 1;
		credentials.pTlsParameters = &tlsParameters;

		status = ::AcquireCredentialsHandleW(nullptr, (LPWSTR) UNISP_NAME_W, usage, nullptr,
											 &credentials, nullptr, nullptr, &_credentials,
											 nullptr);
#endif

		if (status != SEC_E_OK)
		{
			// Fall back to the legacy structure, which caps the connection at TLS 1.2
			SCHANNEL_CRED legacyCredentials;
			std::memset(&legacyCredentials, 0, sizeof(legacyCredentials));
			legacyCredentials.dwVersion = SCHANNEL_CRED_VERSION;
			legacyCredentials.dwFlags = flags;
			legacyCredentials.cCreds = certificateCount;
			legacyCredentials.paCred = (certificateCount > 0) ? certificates : nullptr;
			legacyCredentials.grbitEnabledProtocols =
				isClient ? SP_PROT_TLS1_2_CLIENT : SP_PROT_TLS1_2_SERVER;

			status = ::AcquireCredentialsHandleW(nullptr, (LPWSTR) UNISP_NAME_W, usage, nullptr,
												 &legacyCredentials, nullptr, nullptr,
												 &_credentials, nullptr);
		}

		if (status != SEC_E_OK)
		{
			errMsg = "Cannot acquire the Schannel credentials: " + describeStatus(status);
			return false;
		}

		_hasCredentials = true;
		return true;
	}

	ssize_t SocketSChannel::readEncrypted()
	{
		if (_encryptedBuffer.size() >= kMaxHandshakeSize)
		{
			::WSASetLastError(WSAEMSGSIZE);
			return -1;
		}

		std::size_t offset = _encryptedBuffer.size();
		_encryptedBuffer.resize(offset + kMaxRecordSize);

		int received = ::recv((SOCKET)(intptr_t)(int) _sockfd, &_encryptedBuffer[offset],
							  (int) kMaxRecordSize, 0);

		_encryptedBuffer.resize(offset + ((received > 0) ? (std::size_t) received : 0));
		return received;
	}

	bool SocketSChannel::sendToken(const void* data,
								   size_t length,
								   std::string& errMsg,
								   const CancellationRequest& isCancellationRequested)
	{
		const char* buffer = (const char*) data;
		std::size_t offset = 0;
		auto deadline =
			std::chrono::steady_clock::now() + std::chrono::milliseconds(kHandshakeTimeoutMs);

		while (offset < length)
		{
			if (isCancellationRequested && isCancellationRequested())
			{
				errMsg = "Cancellation requested";
				return false;
			}

			if (std::chrono::steady_clock::now() > deadline)
			{
				errMsg = "Timed out sending a TLS handshake token";
				return false;
			}

			int sent = ::send((SOCKET)(intptr_t)(int) _sockfd, buffer + offset,
							  (int)(length - offset), 0);
			if (sent > 0)
			{
				offset += (std::size_t) sent;
				continue;
			}

			if (sent < 0 && Socket::isWaitNeeded())
			{
				if (Socket::poll(false, kHandshakePollTimeoutMs, _sockfd, nullptr) ==
					PollResultType::Error)
				{
					errMsg = "Connection error while sending a TLS handshake token";
					return false;
				}
				continue;
			}

			errMsg = "Cannot send a TLS handshake token";
			return false;
		}

		return true;
	}

	bool SocketSChannel::performHandshake(std::string& errMsg,
										  const CancellationRequest& isCancellationRequested)
	{
		unsigned long requestFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
									 ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY |
									 ISC_RET_EXTENDED_ERROR | ISC_REQ_STREAM;
		if (_manualValidation)
		{
			requestFlags |= ISC_REQ_MANUAL_CRED_VALIDATION;
		}

		unsigned long acceptFlags = ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT |
									ASC_REQ_CONFIDENTIALITY | ASC_REQ_ALLOCATE_MEMORY |
									ASC_RET_EXTENDED_ERROR | ASC_REQ_STREAM;
		if (!_tlsOptions.isPeerVerifyDisabled())
		{
			acceptFlags |= ASC_REQ_MUTUAL_AUTH;
		}

		LPWSTR target = _targetName.empty() ? nullptr : &_targetName[0];
		auto deadline =
			std::chrono::steady_clock::now() + std::chrono::milliseconds(kHandshakeTimeoutMs);
		SECURITY_STATUS status = SEC_I_CONTINUE_NEEDED;

		if (_isClient && !_hasContext)
		{
			// The client opens the exchange with a token that isn't a reply to anything
			SecBuffer outputBuffer = {0, SECBUFFER_TOKEN, nullptr};
			SecBufferDesc output = {SECBUFFER_VERSION, 1, &outputBuffer};
			unsigned long contextAttributes = 0;

			status = ::InitializeSecurityContextW(&_credentials, nullptr, target, requestFlags, 0, 0,
												  nullptr, 0, &_context, &output,
												  &contextAttributes, nullptr);
			if (status == SEC_I_CONTINUE_NEEDED)
			{
				_hasContext = true;
			}

			bool sent = true;
			if (outputBuffer.cbBuffer > 0 && outputBuffer.pvBuffer != nullptr)
			{
				sent = sendToken(outputBuffer.pvBuffer, outputBuffer.cbBuffer, errMsg,
								 isCancellationRequested);
				::FreeContextBuffer(outputBuffer.pvBuffer);
			}

			if (!sent)
			{
				return false;
			}

			if (status != SEC_I_CONTINUE_NEEDED)
			{
				errMsg = "Cannot start the TLS handshake: " + describeStatus(status);
				return false;
			}
		}

		while (true)
		{
			if (isCancellationRequested && isCancellationRequested())
			{
				errMsg = "Cancellation requested";
				return false;
			}

			if (std::chrono::steady_clock::now() > deadline)
			{
				errMsg = "The TLS handshake timed out";
				return false;
			}

			if (_encryptedBuffer.empty() || status == SEC_E_INCOMPLETE_MESSAGE)
			{
				ssize_t received = readEncrypted();
				if (received == 0)
				{
					errMsg = "The connection was closed during the TLS handshake";
					return false;
				}
				if (received < 0)
				{
					if (!Socket::isWaitNeeded())
					{
						errMsg = "Connection error during the TLS handshake";
						return false;
					}

					if (Socket::poll(true, kHandshakePollTimeoutMs, _sockfd, nullptr) ==
						PollResultType::Error)
					{
						errMsg = "Connection error during the TLS handshake";
						return false;
					}
					continue;
				}
			}

			SecBuffer inputBuffers[2];
			inputBuffers[0].cbBuffer = (unsigned long) _encryptedBuffer.size();
			inputBuffers[0].BufferType = SECBUFFER_TOKEN;
			inputBuffers[0].pvBuffer = &_encryptedBuffer[0];
			inputBuffers[1].cbBuffer = 0;
			inputBuffers[1].BufferType = SECBUFFER_EMPTY;
			inputBuffers[1].pvBuffer = nullptr;
			SecBufferDesc input = {SECBUFFER_VERSION, 2, inputBuffers};

			SecBuffer outputBuffer = {0, SECBUFFER_TOKEN, nullptr};
			SecBufferDesc output = {SECBUFFER_VERSION, 1, &outputBuffer};
			unsigned long contextAttributes = 0;

			if (_isClient)
			{
				status = ::InitializeSecurityContextW(&_credentials, &_context, target,
													  requestFlags, 0, 0, &input, 0, nullptr,
													  &output, &contextAttributes, nullptr);
			}
			else
			{
				status = ::AcceptSecurityContext(&_credentials, _hasContext ? &_context : nullptr,
												 &input, acceptFlags, 0,
												 _hasContext ? nullptr : &_context, &output,
												 &contextAttributes, nullptr);
			}

			// The output can hold an alert that tells the peer why the handshake is failing
			if (outputBuffer.cbBuffer > 0 && outputBuffer.pvBuffer != nullptr)
			{
				std::string sendErrMsg;
				sendToken(outputBuffer.pvBuffer, outputBuffer.cbBuffer, sendErrMsg,
						  isCancellationRequested);
				::FreeContextBuffer(outputBuffer.pvBuffer);
			}

			if (status == SEC_E_INCOMPLETE_MESSAGE)
			{
				// The record is only partially received, so it has to be presented again as a whole
				continue;
			}

			if (status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED)
			{
				_hasContext = true;

				// Whatever wasn't part of the handshake belongs to the next message
				if (inputBuffers[1].BufferType == SECBUFFER_EXTRA && inputBuffers[1].cbBuffer > 0 &&
					inputBuffers[1].cbBuffer <= _encryptedBuffer.size())
				{
					std::size_t extra = inputBuffers[1].cbBuffer;
					std::memmove(&_encryptedBuffer[0],
								 &_encryptedBuffer[_encryptedBuffer.size() - extra], extra);
					_encryptedBuffer.resize(extra);
				}
				else
				{
					_encryptedBuffer.clear();
				}

				if (status == SEC_E_OK)
				{
					break;
				}
				continue;
			}

			if (status == SEC_I_INCOMPLETE_CREDENTIALS)
			{
				errMsg = "The peer requested a certificate that wasn't configured";
				return false;
			}

			errMsg = "The TLS handshake failed: " + describeStatus(status);
			return false;
		}

		status = ::QueryContextAttributesW(&_context, SECPKG_ATTR_STREAM_SIZES, &_streamSizes);
		if (status != SEC_E_OK)
		{
			errMsg = "Cannot query the TLS stream sizes: " + describeStatus(status);
			return false;
		}

		if (_manualValidation && !_tlsOptions.isPeerVerifyDisabled())
		{
			return verifyPeerCertificate(errMsg);
		}

		return true;
	}

	bool SocketSChannel::verifyPeerCertificate(std::string& errMsg)
	{
		PCCERT_CONTEXT peerCertificate = nullptr;
		SECURITY_STATUS status = ::QueryContextAttributesW(&_context,
														   SECPKG_ATTR_REMOTE_CERT_CONTEXT,
														   &peerCertificate);
		if (status != SEC_E_OK || peerCertificate == nullptr)
		{
			errMsg = "The peer didn't present a certificate: " + describeStatus(status);
			return false;
		}

		LPSTR usages[1] = {_isClient ? (LPSTR) szOID_PKIX_KP_SERVER_AUTH
									 : (LPSTR) szOID_PKIX_KP_CLIENT_AUTH};

		CERT_CHAIN_PARA chainParameters;
		std::memset(&chainParameters, 0, sizeof(chainParameters));
		chainParameters.cbSize = sizeof(chainParameters);
		chainParameters.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
		chainParameters.RequestedUsage.Usage.cUsageIdentifier = 1;
		chainParameters.RequestedUsage.Usage.rgpszUsageIdentifier = usages;

		PCCERT_CHAIN_CONTEXT chain = nullptr;
		bool built = ::CertGetCertificateChain(_chainEngine, peerCertificate, nullptr,
											   peerCertificate->hCertStore, &chainParameters, 0,
											   nullptr, &chain) != FALSE;
		if (!built)
		{
			errMsg = "Cannot build the certificate chain of the peer: " +
					 describeStatus((SECURITY_STATUS)::GetLastError());
			::CertFreeCertificateContext(peerCertificate);
			return false;
		}

		std::wstring serverName = _targetName;

		SSL_EXTRA_CERT_CHAIN_POLICY_PARA sslPolicy;
		std::memset(&sslPolicy, 0, sizeof(sslPolicy));
		sslPolicy.cbSize = sizeof(sslPolicy);
		sslPolicy.dwAuthType = _isClient ? AUTHTYPE_SERVER : AUTHTYPE_CLIENT;
		if (!_isClient || _tlsOptions.disable_hostname_validation || serverName.empty())
		{
			sslPolicy.fdwChecks = kIgnoreCertificateNameMismatch;
		}
		else
		{
			sslPolicy.pwszServerName = &serverName[0];
		}

		CERT_CHAIN_POLICY_PARA policyParameters;
		std::memset(&policyParameters, 0, sizeof(policyParameters));
		policyParameters.cbSize = sizeof(policyParameters);
		policyParameters.pvExtraPolicyPara = &sslPolicy;

		CERT_CHAIN_POLICY_STATUS policyStatus;
		std::memset(&policyStatus, 0, sizeof(policyStatus));
		policyStatus.cbSize = sizeof(policyStatus);

		bool verified = ::CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain,
														   &policyParameters, &policyStatus) != FALSE;
		DWORD policyError = policyStatus.dwError;

		::CertFreeCertificateChain(chain);
		::CertFreeCertificateContext(peerCertificate);

		if (!verified)
		{
			errMsg = "Cannot verify the certificate of the peer: " +
					 describeStatus((SECURITY_STATUS)::GetLastError());
			return false;
		}

		if (policyError != 0)
		{
			errMsg = "The certificate of the peer was rejected: " +
					 describeStatus((SECURITY_STATUS) policyError);
			return false;
		}

		return true;
	}

	bool SocketSChannel::accept(std::string& errMsg)
	{
		std::lock_guard<std::mutex> lock(_mutex);

		if (_sockfd == -1)
		{
			errMsg = "Socket is uninitialized";
			return false;
		}

		if (!_tlsOptions.hasCertAndKey())
		{
			errMsg = "A certificate and a key are required to accept TLS connections";
			return false;
		}

		_isClient = false;

		if (!acquireCredentials(false, errMsg))
		{
			return false;
		}

		return performHandshake(errMsg, nullptr);
	}

	bool SocketSChannel::connect(const std::string& host,
								 int port,
								 std::string& errMsg,
								 const CancellationRequest& isCancellationRequested)
	{
		std::lock_guard<std::mutex> lock(_mutex);

		_isClient = true;
		_host = host;
		_targetName = toWideString(host);

		if (!acquireCredentials(true, errMsg))
		{
			return false;
		}

		_sockfd = SocketConnect::connect(host, port, errMsg, isCancellationRequested);
		if (_sockfd == -1)
		{
			return false;
		}

		return performHandshake(errMsg, isCancellationRequested);
	}

	void SocketSChannel::sendCloseNotify()
	{
		if (!_hasContext || _shutdownSent || _sockfd == -1)
		{
			return;
		}

		_shutdownSent = true;

		DWORD token = SCHANNEL_SHUTDOWN;
		SecBuffer tokenBuffer = {sizeof(token), SECBUFFER_TOKEN, &token};
		SecBufferDesc tokenDescription = {SECBUFFER_VERSION, 1, &tokenBuffer};

		if (::ApplyControlToken(&_context, &tokenDescription) != SEC_E_OK)
		{
			return;
		}

		SecBuffer outputBuffer = {0, SECBUFFER_TOKEN, nullptr};
		SecBufferDesc output = {SECBUFFER_VERSION, 1, &outputBuffer};
		unsigned long contextAttributes = 0;
		SECURITY_STATUS status;

		if (_isClient)
		{
			unsigned long requestFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
										 ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY |
										 ISC_RET_EXTENDED_ERROR | ISC_REQ_STREAM;
			LPWSTR target = _targetName.empty() ? nullptr : &_targetName[0];
			status = ::InitializeSecurityContextW(&_credentials, &_context, target, requestFlags, 0,
												  0, nullptr, 0, nullptr, &output,
												  &contextAttributes, nullptr);
		}
		else
		{
			unsigned long acceptFlags = ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT |
										ASC_REQ_CONFIDENTIALITY | ASC_REQ_ALLOCATE_MEMORY |
										ASC_RET_EXTENDED_ERROR | ASC_REQ_STREAM;
			status = ::AcceptSecurityContext(&_credentials, &_context, nullptr, acceptFlags, 0,
											 nullptr, &output, &contextAttributes, nullptr);
		}

		if ((status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED) && outputBuffer.cbBuffer > 0 &&
			outputBuffer.pvBuffer != nullptr)
		{
			// The alert is a courtesy, so it's sent in one attempt and never waited for
			::send((SOCKET)(intptr_t)(int) _sockfd, (const char*) outputBuffer.pvBuffer,
				   (int) outputBuffer.cbBuffer, 0);
		}

		if (outputBuffer.pvBuffer != nullptr)
		{
			::FreeContextBuffer(outputBuffer.pvBuffer);
		}
	}

	void SocketSChannel::freeSecurityResources()
	{
		if (_hasContext)
		{
			::DeleteSecurityContext(&_context);
			SecInvalidateHandle(&_context);
			_hasContext = false;
		}

		if (_hasCredentials)
		{
			::FreeCredentialsHandle(&_credentials);
			SecInvalidateHandle(&_credentials);
			_hasCredentials = false;
		}

		if (_chainEngine != nullptr)
		{
			::CertFreeCertificateChainEngine(_chainEngine);
			_chainEngine = nullptr;
		}

		if (_caStore != nullptr)
		{
			::CertCloseStore(_caStore, 0);
			_caStore = nullptr;
		}

		// The certificate is shared with the other sockets of the process, so this only gives up
		// the reference that this one holds
		_certificate.reset();

		_encryptedBuffer.clear();
		_plainBuffer.clear();
		_plainOffset = 0;
		_pendingOutput.clear();
		_pendingOffset = 0;
		_pendingPlainLength = 0;
	}

	void SocketSChannel::close()
	{
		{
			std::lock_guard<std::mutex> lock(_mutex);

			sendCloseNotify();
			freeSecurityResources();
		}

		Socket::close();
	}

	bool SocketSChannel::hasPendingInput() const
	{
		if (_plainOffset < _plainBuffer.size())
		{
			return true;
		}

		// A record that was received in full can be decrypted without touching the socket again
		if (_encryptedBuffer.size() >= kRecordHeaderSize)
		{
			std::size_t payload = ((unsigned char) _encryptedBuffer[3] << 8) |
								  (unsigned char) _encryptedBuffer[4];
			return _encryptedBuffer.size() >= kRecordHeaderSize + payload;
		}

		return false;
	}

	bool SocketSChannel::flushPendingOutput()
	{
		while (_pendingOffset < _pendingOutput.size())
		{
			if (_sockfd == -1)
			{
				return false;
			}

			int sent = ::send((SOCKET)(intptr_t)(int) _sockfd, &_pendingOutput[_pendingOffset],
							  (int)(_pendingOutput.size() - _pendingOffset), 0);
			if (sent <= 0)
			{
				return false;
			}

			_pendingOffset += (std::size_t) sent;
		}

		_pendingOutput.clear();
		_pendingOffset = 0;
		return true;
	}

	ssize_t SocketSChannel::send(char* buf, size_t nbyte)
	{
		std::lock_guard<std::mutex> lock(_mutex);

		if (!_hasContext || _sockfd == -1)
		{
			return 0;
		}

		if (nbyte == 0)
		{
			return 0;
		}

		// A record cannot be split, so the previous one has to be out before another one is made
		if (!flushPendingOutput())
		{
			if (Socket::isWaitNeeded())
			{
				::WSASetLastError(WSAEWOULDBLOCK);
			}
			return -1;
		}

		if (_pendingPlainLength > 0)
		{
			// The record that has just been written encrypted the beginning of this payload, which
			// the caller repeats until it is told how much of it was taken
			std::size_t sent = (_pendingPlainLength < nbyte) ? _pendingPlainLength : nbyte;
			_pendingPlainLength = 0;
			return (ssize_t) sent;
		}

		std::size_t length = nbyte;
		if (length > _streamSizes.cbMaximumMessage)
		{
			length = _streamSizes.cbMaximumMessage;
		}

		std::vector<char> record(_streamSizes.cbHeader + length + _streamSizes.cbTrailer);
		std::memcpy(&record[_streamSizes.cbHeader], buf, length);

		SecBuffer buffers[4];
		buffers[0].cbBuffer = _streamSizes.cbHeader;
		buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
		buffers[0].pvBuffer = &record[0];
		buffers[1].cbBuffer = (unsigned long) length;
		buffers[1].BufferType = SECBUFFER_DATA;
		buffers[1].pvBuffer = &record[_streamSizes.cbHeader];
		buffers[2].cbBuffer = _streamSizes.cbTrailer;
		buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
		buffers[2].pvBuffer = &record[_streamSizes.cbHeader + length];
		buffers[3].cbBuffer = 0;
		buffers[3].BufferType = SECBUFFER_EMPTY;
		buffers[3].pvBuffer = nullptr;
		SecBufferDesc description = {SECBUFFER_VERSION, 4, buffers};

		SECURITY_STATUS status = ::EncryptMessage(&_context, 0, &description, 0);
		if (status != SEC_E_OK)
		{
			::WSASetLastError(WSAECONNABORTED);
			return -1;
		}

		std::size_t recordSize = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
		std::size_t offset = 0;

		while (offset < recordSize)
		{
			int sent = ::send((SOCKET)(intptr_t)(int) _sockfd, &record[offset],
							  (int)(recordSize - offset), 0);
			if (sent > 0)
			{
				offset += (std::size_t) sent;
				continue;
			}

			if (sent < 0 && !Socket::isWaitNeeded())
			{
				return -1;
			}
			break;
		}

		if (offset < recordSize)
		{
			// Reporting the payload as sent while a part of its record is still here would let the
			// caller move on and leave that remainder behind, so it is reported once it is out
			_pendingOutput.assign(record.begin() + offset, record.begin() + recordSize);
			_pendingOffset = 0;
			_pendingPlainLength = length;

			::WSASetLastError(WSAEWOULDBLOCK);
			return -1;
		}

		return (ssize_t) length;
	}

	ssize_t SocketSChannel::recv(void* buf, size_t nbyte)
	{
		std::lock_guard<std::mutex> lock(_mutex);

		// Every poll of the transport is an opportunity to get rid of a partially sent record
		flushPendingOutput();

		while (true)
		{
			if (_plainOffset < _plainBuffer.size())
			{
				std::size_t available = _plainBuffer.size() - _plainOffset;
				std::size_t length = (nbyte < available) ? nbyte : available;
				std::memcpy(buf, &_plainBuffer[_plainOffset], length);
				_plainOffset += length;

				if (_plainOffset == _plainBuffer.size())
				{
					_plainBuffer.clear();
					_plainOffset = 0;
				}

				// A poll of the caller watches the socket, which stays silent while the rest of
				// what was received is waiting here, so it has to be woken up by hand
				if (hasPendingInput())
				{
					wakeUpFromPoll(kBufferedDataRequest);
				}

				return (ssize_t) length;
			}

			if (!_hasContext || _sockfd == -1 || _peerClosed)
			{
				return 0;
			}

			if (!_encryptedBuffer.empty())
			{
				SecBuffer buffers[4];
				buffers[0].cbBuffer = (unsigned long) _encryptedBuffer.size();
				buffers[0].BufferType = SECBUFFER_DATA;
				buffers[0].pvBuffer = &_encryptedBuffer[0];
				for (int i = 1; i < 4; i++)
				{
					buffers[i].cbBuffer = 0;
					buffers[i].BufferType = SECBUFFER_EMPTY;
					buffers[i].pvBuffer = nullptr;
				}
				SecBufferDesc description = {SECBUFFER_VERSION, 4, buffers};

				SECURITY_STATUS status = ::DecryptMessage(&_context, &description, 0, nullptr);

				if (status == SEC_E_OK || status == SEC_I_RENEGOTIATE ||
					status == SEC_I_CONTEXT_EXPIRED)
				{
					const char* extra = nullptr;
					std::size_t extraSize = 0;

					for (int i = 1; i < 4; i++)
					{
						if (buffers[i].BufferType == SECBUFFER_DATA && buffers[i].cbBuffer > 0)
						{
							const char* data = (const char*) buffers[i].pvBuffer;
							_plainBuffer.insert(_plainBuffer.end(), data,
												data + buffers[i].cbBuffer);
						}
						else if (buffers[i].BufferType == SECBUFFER_EXTRA && buffers[i].cbBuffer > 0)
						{
							extra = (const char*) buffers[i].pvBuffer;
							extraSize = buffers[i].cbBuffer;
						}
					}

					if (extra != nullptr && extraSize > 0)
					{
						std::memmove(&_encryptedBuffer[0], extra, extraSize);
						_encryptedBuffer.resize(extraSize);
					}
					else
					{
						_encryptedBuffer.clear();
					}

					if (status == SEC_I_CONTEXT_EXPIRED)
					{
						// The peer closed the connection in an orderly way
						_peerClosed = true;
						continue;
					}

					if (status == SEC_I_RENEGOTIATE)
					{
						// Post-handshake messages, such as the session tickets of TLS 1.3, are
						// handed back through the same calls that established the context
						std::string errMsg;
						if (!performHandshake(errMsg, nullptr))
						{
							::WSASetLastError(WSAECONNABORTED);
							return -1;
						}
					}

					continue;
				}

				if (status != SEC_E_INCOMPLETE_MESSAGE)
				{
					::WSASetLastError(WSAECONNABORTED);
					return -1;
				}
			}

			ssize_t received = readEncrypted();
			if (received > 0)
			{
				continue;
			}

			if (received == 0)
			{
				_peerClosed = true;
				return 0;
			}

			if (Socket::isWaitNeeded())
			{
				::WSASetLastError(WSAEWOULDBLOCK);
			}
			return -1;
		}
	}
}

#endif // IXWEBSOCKET_USE_SCHANNEL
