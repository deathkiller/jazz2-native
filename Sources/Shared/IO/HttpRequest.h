#pragma once

#include "../CommonBase.h"

#if !defined(DEATH_TARGET_EMSCRIPTEN)

#include <cstring>
#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <type_traits>

#include "../Containers/SmallVector.h"
#include "../Containers/String.h"
#include "../Containers/StringView.h"

#if defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_CYGWIN)
#	pragma comment(lib, "Ws2_32")
#	pragma push_macro("WIN32_LEAN_AND_MEAN")
#	pragma push_macro("NOMINMAX")
#	if !defined(WIN32_LEAN_AND_MEAN)
#		define WIN32_LEAN_AND_MEAN
#	endif
#	if !defined(NOMINMAX)
#		define NOMINMAX
#	endif
#	include <ws2tcpip.h>
#	pragma pop_macro("WIN32_LEAN_AND_MEAN")
#	pragma pop_macro("NOMINMAX")
#else
#	include <errno.h>
#	include <fcntl.h>
#	include <netinet/in.h>
#	include <netdb.h>
#	include <sys/select.h>
#	include <sys/socket.h>
#	include <sys/types.h>
#	include <unistd.h>
#endif

namespace Death { namespace IO { namespace Http {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	using Containers::Literals::operator"" _s;

	enum class InternetProtocol : std::uint8_t {
		V4,
		V6
	};

	struct Uri final {
		Containers::String Scheme;
		Containers::String Host;
		Containers::String Port;
		Containers::String Path;
		Containers::String Query;
		Containers::String Fragment;
	};

	struct HttpVersion final {
		std::uint16_t Major;
		std::uint16_t Minor;
	};

	struct HttpStatus final	{
		// RFC 7231, 6. Response Status Codes
		enum Code : std::uint16_t {
			Continue = 100,
			SwitchingProtocol = 101,
			Processing = 102,
			EarlyHints = 103,

			Ok = 200,
			Created = 201,
			Accepted = 202,
			NonAuthoritativeInformation = 203,
			NoContent = 204,
			ResetContent = 205,
			PartialContent = 206,
			MultiStatus = 207,
			AlreadyReported = 208,
			ImUsed = 226,

			MultipleChoice = 300,
			MovedPermanently = 301,
			Found = 302,
			SeeOther = 303,
			NotModified = 304,
			UseProxy = 305,
			TemporaryRedirect = 307,
			PermanentRedirect = 308,

			BadRequest = 400,
			Unauthorized = 401,
			PaymentRequired = 402,
			Forbidden = 403,
			NotFound = 404,
			MethodNotAllowed = 405,
			NotAcceptable = 406,
			ProxyAuthenticationRequired = 407,
			RequestTimeout = 408,
			Conflict = 409,
			Gone = 410,
			LengthRequired = 411,
			PreconditionFailed = 412,
			PayloadTooLarge = 413,
			UriTooLong = 414,
			UnsupportedMediaType = 415,
			RangeNotSatisfiable = 416,
			ExpectationFailed = 417,
			MisdirectedRequest = 421,
			UnprocessableEntity = 422,
			Locked = 423,
			FailedDependency = 424,
			TooEarly = 425,
			UpgradeRequired = 426,
			PreconditionRequired = 428,
			TooManyRequests = 429,
			RequestHeaderFieldsTooLarge = 431,
			UnavailableForLegalReasons = 451,

			InternalServerError = 500,
			NotImplemented = 501,
			BadGateway = 502,
			ServiceUnavailable = 503,
			GatewayTimeout = 504,
			HttpVersionNotSupported = 505,
			VariantAlsoNegotiates = 506,
			InsufficientStorage = 507,
			LoopDetected = 508,
			NotExtended = 510,
			NetworkAuthenticationRequired = 511,

			ResponseCorrupted = 1000
		};

		HttpVersion Version;
		std::uint16_t Code;
		Containers::String Reason;
	};

	using HeaderField = std::pair<Containers::String, Containers::String>;

	struct Response final {
		HttpStatus Status;
		Containers::SmallVector<HeaderField, 0> HeaderFields;
		Containers::SmallVector<std::uint8_t, 0> Body;
	};

#if defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_CYGWIN)
	class WinSock final
	{
	public:
		WinSock()
		{
			WSADATA wsaData;
			const auto error = WSAStartup(MAKEWORD(2, 2), &wsaData);
			if (error != 0) {
				return;
			}
			if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
				WSACleanup();
				return;
			}

			_started = true;
		}

		WinSock(WinSock&& other) noexcept
			: _started { other._started }
		{
			other._started = false;
		}

		~WinSock()
		{
			if (_started) {
				WSACleanup();
			}
		}

		WinSock& operator=(WinSock&& other) noexcept
		{
			if (&other == this) return *this;
			if (_started) WSACleanup();
			_started = other._started;
			other._started = false;
			return *this;
		}

	private:
		bool _started = false;
	};
#endif

	inline int GetLastError() noexcept
	{
#if defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_CYGWIN)
		return WSAGetLastError();
#else
		return errno;
#endif
	}

	constexpr int GetAddressFamily(const InternetProtocol internetProtocol)
	{
		return (internetProtocol == InternetProtocol::V4 ? AF_INET : (internetProtocol == InternetProtocol::V6 ? AF_INET6 : 0));
	}

	class Socket final
	{
	public:
#if defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_CYGWIN)
		using Type = SOCKET;
		static constexpr Type invalid = INVALID_SOCKET;
#else
		using Type = int;
		static constexpr Type invalid = -1;
#endif

		explicit Socket(const InternetProtocol internetProtocol)
			: endpoint { socket(GetAddressFamily(internetProtocol), SOCK_STREAM, IPPROTO_TCP) }
		{
			if (endpoint == invalid) {
				return;
			}

#if defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_CYGWIN)
			ULONG mode = 1;
			if (::ioctlsocket(endpoint, FIONBIO, &mode) != 0) {
				Close();
				endpoint = invalid;
				return;
			}
#else
			const auto flags = ::fcntl(endpoint, F_GETFL);
			if (flags == -1) {
				Close();
				endpoint = invalid;
				return;
			}

			if (::fcntl(endpoint, F_SETFL, flags | O_NONBLOCK) == -1) {
				Close();
				endpoint = invalid;
				return;
			}
#endif

#if defined(DEATH_TARGET_APPLE)
			const int value = 1;
			if (::setsockopt(endpoint, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(value)) == -1) {
				Close();
				endpoint = invalid;
				return;
			}
#endif
		}

		Socket(Socket&& other) noexcept
			: endpoint { other.endpoint }
		{
			other.endpoint = invalid;
		}

		~Socket()
		{
			if (endpoint != invalid) Close();
		}

		Socket& operator=(Socket&& other) noexcept
		{
			if (&other == this) return *this;
			if (endpoint != invalid) Close();
			endpoint = other.endpoint;
			other.endpoint = invalid;
			return *this;
		}

		bool Connect(const struct sockaddr* address, const socklen_t addressSize, const std::int64_t timeout)
		{
			if (endpoint == invalid) {
				return false;
			}

#if defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_CYGWIN)
			auto result = ::connect(endpoint, address, addressSize);
			while (result == -1 && WSAGetLastError() == WSAEINTR) {
				result = ::connect(endpoint, address, addressSize);
			}
			if (result == -1) {
				if (WSAGetLastError() == WSAEWOULDBLOCK && Select(SelectType::Write, timeout)) {
					int socketError;
					socklen_t optionLength = sizeof(socketError);
					if (::getsockopt(endpoint, SOL_SOCKET, SO_ERROR, (char*)&socketError, &optionLength) == -1) {
						return false;
					}
					return (socketError == 0);
				}

				return false;
			}
#else
			auto result = ::connect(endpoint, address, addressSize);
			while (result == -1 && errno == EINTR) {
				result = ::connect(endpoint, address, addressSize);
			}
			if (result == -1) {
				if (errno == EINPROGRESS && Select(SelectType::Write, timeout)) {
					int socketError;
					socklen_t optionLength = sizeof(socketError);
					if (::getsockopt(endpoint, SOL_SOCKET, SO_ERROR, &socketError, &optionLength) == -1) {
						return false;
					}

					return (socketError == 0);
				}
					
				return false;
			}
#endif
			return true;
		}

		std::size_t Send(const void* buffer, const std::size_t length, const std::int64_t timeout)
		{
			if (!Select(SelectType::Write, timeout)) {
				return 0;
			}
#if defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_CYGWIN)
			auto result = ::send(endpoint, reinterpret_cast<const char*>(buffer), static_cast<int>(length), 0);
			while (result == -1 && WSAGetLastError() == WSAEINTR) {
				result = ::send(endpoint, reinterpret_cast<const char*>(buffer), static_cast<int>(length), 0);
			}
#else
			auto result = ::send(endpoint, reinterpret_cast<const char*>(buffer), length, noSignal);
			while (result == -1 && errno == EINTR) {
				result = ::send(endpoint, reinterpret_cast<const char*>(buffer), length, noSignal);
			}
#endif
			return static_cast<std::size_t>(result);
		}

		std::size_t Recv(void* buffer, const std::size_t length, const std::int64_t timeout)
		{
			if (!Select(SelectType::Read, timeout)) {
				return 0;
			}
#if defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_CYGWIN)
			auto result = ::recv(endpoint, reinterpret_cast<char*>(buffer), static_cast<int>(length), 0);
			while (result == -1 && WSAGetLastError() == WSAEINTR) {
				result = ::recv(endpoint, reinterpret_cast<char*>(buffer), static_cast<int>(length), 0);
			}
#else
			auto result = ::recv(endpoint, reinterpret_cast<char*>(buffer), length, noSignal);
			while (result == -1 && errno == EINTR) {
				result = ::recv(endpoint, reinterpret_cast<char*>(buffer), length, noSignal);
			}
#endif
			return static_cast<std::size_t>(result);
		}

		bool IsValid() const
		{
			return (endpoint != invalid);
		}

	private:
		enum class SelectType {
			Read,
			Write
		};

		bool Select(const SelectType type, const std::int64_t timeout)
		{
			fd_set descriptorSet;
			FD_ZERO(&descriptorSet);
			FD_SET(endpoint, &descriptorSet);

#if defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_CYGWIN)
			TIMEVAL selectTimeout {
				static_cast<LONG>(timeout / 1000),
				static_cast<LONG>((timeout % 1000) * 1000)
			};
			auto count = ::select(0,
									(type == SelectType::Read) ? &descriptorSet : nullptr,
									(type == SelectType::Write) ? &descriptorSet : nullptr,
									nullptr,
									timeout >= 0 ? &selectTimeout : nullptr);

			while (count == -1 && WSAGetLastError() == WSAEINTR) {
				count = ::select(0,
									(type == SelectType::Read) ? &descriptorSet : nullptr,
									(type == SelectType::Write) ? &descriptorSet : nullptr,
									nullptr,
									timeout >= 0 ? &selectTimeout : nullptr);
			}
			if (count <= 0) {
				// Failed to select socket or request timed out
				return false;
			}
#else
			timeval selectTimeout {
				static_cast<time_t>(timeout / 1000),
				static_cast<suseconds_t>((timeout % 1000) * 1000)
			};
			auto count = ::select(endpoint + 1,
									(type == SelectType::Read) ? &descriptorSet : nullptr,
									(type == SelectType::Write) ? &descriptorSet : nullptr,
									nullptr,
									timeout >= 0 ? &selectTimeout : nullptr);

			while (count == -1 && errno == EINTR) {
				count = ::select(endpoint + 1,
									(type == SelectType::Read) ? &descriptorSet : nullptr,
									(type == SelectType::Write) ? &descriptorSet : nullptr,
									nullptr,
									timeout >= 0 ? &selectTimeout : nullptr);
			}
			if (count <= 0) {
				// Failed to select socket or request timed out
				return false;
			}
#endif
			return true;
		}

		void Close() noexcept
		{
#if defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_CYGWIN)
			::closesocket(endpoint);
#else
			::close(endpoint);
#endif
		}

#if defined(DEATH_TARGET_UNIX) && !defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_CYGWIN)
		static constexpr int noSignal = MSG_NOSIGNAL;
#else
		static constexpr int noSignal = 0;
#endif

		Type endpoint = invalid;
	};

	// RFC 7230, 3.2.3. WhiteSpace
	template <typename C>
	constexpr bool IsWhiteSpaceChar(const C c) noexcept
	{
		return (c == 0x20 || c == 0x09);	// space or tab
	};

	// RFC 5234, Appendix B.1. Core Rules
	template <typename C>
	constexpr bool IsDigitChar(const C c) noexcept
	{
		return (c >= 0x30 && c <= 0x39);	// 0 - 9
	}

	// RFC 5234, Appendix B.1. Core Rules
	template <typename C>
	constexpr bool IsAlphaChar(const C c) noexcept
	{
		return
			(c >= 0x61 && c <= 0x7A) ||		// a - z
			(c >= 0x41 && c <= 0x5A);		// A - Z
	}

	// RFC 7230, 3.2.6. Field Value Components
	template <typename C>
	constexpr bool IsTokenChar(const C c) noexcept
	{
		return c == 0x21 || // !
			   c == 0x23 || // #
			   c == 0x24 || // $
			   c == 0x25 || // %
			   c == 0x26 || // &
			   c == 0x27 || // '
			   c == 0x2A || // *
			   c == 0x2B || // +
			   c == 0x2D || // -
			   c == 0x2E || // .
			   c == 0x5E || // ^
			   c == 0x5F || // _
			   c == 0x60 || // `
			   c == 0x7C || // |
			   c == 0x7E || // ~
			   IsDigitChar(c) ||
			   IsAlphaChar(c);
	};

	// RFC 5234, Appendix B.1. Core Rules
	template <typename C>
	constexpr bool IsVisibleChar(const C c) noexcept
	{
		return (c >= 0x21 && c <= 0x7E);
	}

	// RFC 7230, Appendix B. Collected ABNF
	template <typename C>
	constexpr bool IsObsoleteTextChar(const C c) noexcept
	{
		return static_cast<unsigned char>(c) >= 0x80 && static_cast<unsigned char>(c) <= 0xFF;
	}

	template <class Iterator>
	Iterator SkipWhiteSpaces(const Iterator begin, const Iterator end)
	{
		auto i = begin;
		for (; i != end; ++i) {
			if (!IsWhiteSpaceChar(*i)) {
				break;
			}
		}
		return i;
	}

	// RFC 5234, Appendix B.1. Core Rules
	template <typename T, typename C, class = typename std::enable_if<std::is_unsigned<T>::value>::type>
	constexpr T DigitToUint(const C c)
	{
		// DIGIT
		return (c >= 0x30 && c <= 0x39) ? static_cast<T>(c - 0x30) : // 0 - 9
			T(0);
	}

	// RFC 5234, Appendix B.1. Core Rules
	template <typename T, typename C, class = typename std::enable_if<std::is_unsigned<T>::value>::type>
	constexpr T HexDigitToUint(const C c)
	{
		// HEXDIG
		return (c >= 0x30 && c <= 0x39) ? static_cast<T>(c - 0x30) : // 0 - 9
				(c >= 0x41 && c <= 0x46) ? static_cast<T>(c - 0x41) + T(10) : // A - Z
				(c >= 0x61 && c <= 0x66) ? static_cast<T>(c - 0x61) + T(10) : // a - z, some services send lower-case hex digits
				T(0);
	}

	// RFC 3986, 3. Syntax Components
	template <class Iterator>
	Uri ParseUri(const Iterator begin, const Iterator end)
	{
		Uri result;

		// RFC 3986, 3.1. Scheme
		auto i = begin;
		if (i == end || !IsAlphaChar(*begin)) {
			return { };
		}

		for (; i != end && (IsAlphaChar(*i) || IsDigitChar(*i) || *i == '+' || *i == '-' || *i == '.'); ++i) {
			// Try to find the end of scheme
		}
		result.Scheme = Containers::String(begin, i - begin);

		if (i == end || *i++ != ':') {
			return { };
		}
		if (i == end || *i++ != '/') {
			return { };
		}
		if (i == end || *i++ != '/') {
			return { };
		}

		// RFC 3986, 3.2. Authority
		auto authority = Containers::StringView(i, end - i);

		// RFC 3986, 3.5. Fragment
		const auto fragmentPosition = authority.find('#');
		if (fragmentPosition != nullptr) {
			result.Fragment = authority.suffix(fragmentPosition.begin() + 1);
			authority = authority.prefix(fragmentPosition.begin());
		}

		// RFC 3986, 3.4. Query
		const auto queryPosition = authority.find('?');
		if (queryPosition != nullptr) {
			result.Query = authority.suffix(queryPosition.begin() + 1);
			authority = authority.prefix(queryPosition.begin());
		}

		// RFC 3986, 3.3. Path
		const auto pathPosition = authority.find('/');
		if (pathPosition != nullptr) {
			// RFC 3986, 3.3. Path
			result.Path = authority.suffix(pathPosition.begin());
			authority = authority.prefix(pathPosition.begin());
		} else {
			result.Path = "/"_s;
		}

		// RFC 3986, 3.2.2. Host
		const auto portPosition = authority.find(':');
		if (portPosition != nullptr) {
			// RFC 3986, 3.2.3. Port
			result.Port = authority.suffix(portPosition.begin() + 1);
			result.Host = authority.prefix(portPosition.begin());
		} else {
			result.Host = authority;
		}

		return result;
	}

	// RFC 7230, 2.6. Protocol Versioning
	template <class Iterator>
	std::pair<Iterator, HttpVersion> ParseHttpVersion(const Iterator begin, const Iterator end)
	{
		auto i = begin;

		if (i == end || *i++ != 'H') {
			return { i, HttpVersion { 0, 0 } };
		}
		if (i == end || *i++ != 'T') {
			return { i, HttpVersion { 0, 0 } };
		}
		if (i == end || *i++ != 'T') {
			return { i, HttpVersion { 0, 0 } };
		}
		if (i == end || *i++ != 'P') {
			return { i, HttpVersion { 0, 0 } };
		}
		if (i == end || *i++ != '/') {
			return { i, HttpVersion { 0, 0 } };
		}
		if (i == end) {
			return { i, HttpVersion { 0, 0 } };
		}
		const auto majorVersion = DigitToUint<std::uint16_t>(*i++);

		if (i == end || *i++ != '.') {
			return { i, HttpVersion { 0, 0 } };
		}
		if (i == end) {
			return { i, HttpVersion { 0, 0 } };
		}
		const auto minorVersion = DigitToUint<std::uint16_t>(*i++);

		return { i, HttpVersion { majorVersion, minorVersion } };
	}

	// RFC 7230, 3.1.2. Status Line
	template <class Iterator>
	std::pair<Iterator, std::uint16_t> ParseStatusCode(const Iterator begin, const Iterator end)
	{
		std::uint16_t result = 0;

		auto i = begin;
		while (i != end && IsDigitChar(*i)) {
			result = static_cast<std::uint16_t>(result * 10U) + DigitToUint<std::uint16_t>(*i++);
		}

		return { i, result };
	}

	// RFC 7230, 3.1.2. Status Line
	template <class Iterator>
	std::pair<Iterator, std::string> ParseReasonPhrase(const Iterator begin, const Iterator end)
	{
		std::string result;

		auto i = begin;
		for (; i != end && (IsWhiteSpaceChar(*i) || IsVisibleChar(*i) || IsObsoleteTextChar(*i)); ++i) {
			result.push_back(static_cast<char>(*i));
		}

		return { i, std::move(result) };
	}

	// RFC 7230, 3.2.6. Field Value Components
	template <class Iterator>
	std::pair<Iterator, std::string> ParseToken(const Iterator begin, const Iterator end)
	{
		std::string result;

		auto i = begin;
		for (; i != end && IsTokenChar(*i); ++i) {
			result.push_back(static_cast<char>(*i));
		}

		return { i, std::move(result) };
	}

	// RFC 7230, 3.2. Header Fields
	template <class Iterator>
	std::pair<Iterator, std::string> ParseFieldValue(const Iterator begin, const Iterator end)
	{
		std::string result;

		auto i = begin;
		for (; i != end && (IsWhiteSpaceChar(*i) || IsVisibleChar(*i) || IsObsoleteTextChar(*i)); ++i) {
			result.push_back(static_cast<char>(*i));
		}

		// Trim white spaces
		result.erase(std::find_if(result.rbegin(), result.rend(), [](const char c) noexcept {
			return !IsWhiteSpaceChar(c);
		}).base(), result.end());

		return { i, std::move(result) };
	}

	// RFC 7230, 3.2. Header Fields
	template <class Iterator>
	std::pair<Iterator, std::string> ParseFieldContent(const Iterator begin, const Iterator end)
	{
		std::string result;

		auto i = begin;
		while (true) {
			const auto fieldValueResult = ParseFieldValue(i, end);
			i = fieldValueResult.first;
			result += fieldValueResult.second;

			// Handle obsolete fold as per RFC 7230, 3.2.4. Field Parsing
			// Obsolete folding is known as linear white space (LWS) in RFC 2616, 2.2 Basic Rules
			auto obsoleteFoldIterator = i;
			if (obsoleteFoldIterator == end || *obsoleteFoldIterator++ != '\r') {
				break;
			}
			if (obsoleteFoldIterator == end || *obsoleteFoldIterator++ != '\n') {
				break;
			}
			if (obsoleteFoldIterator == end || !IsWhiteSpaceChar(*obsoleteFoldIterator++)) {
				break;
			}
			result.push_back(' ');
			i = obsoleteFoldIterator;
		}

		return { i, std::move(result) };
	}

	// RFC 7230, 3.2. Header Fields
	template <class Iterator>
	std::pair<Iterator, HeaderField> ParseHeaderField(const Iterator begin, const Iterator end)
	{
		auto tokenResult = ParseToken(begin, end);
		auto i = tokenResult.first;
		auto fieldName = std::move(tokenResult.second);

		if (i == end || *i++ != ':') {
			return { i, { } };
		}

		i = SkipWhiteSpaces(i, end);

		auto valueResult = ParseFieldContent(i, end);
		i = valueResult.first;
		auto fieldValue = std::move(valueResult.second);

		if (i == end || *i++ != '\r') {
			return { i, { } };
		}
		if (i == end || *i++ != '\n') {
			return { i, { } };
		}

		return { i, { std::move(fieldName), std::move(fieldValue) } };
	}

	// RFC 7230, 3.1.2. Status Line
	template <class Iterator>
	std::pair<Iterator, HttpStatus> ParseStatusLine(const Iterator begin, const Iterator end)
	{
		const auto httpVersionResult = ParseHttpVersion(begin, end);
		auto i = httpVersionResult.first;

		if (i == end || *i++ != ' ') {
			return { i, { } };
		}

		const auto statusCodeResult = ParseStatusCode(i, end);
		i = statusCodeResult.first;

		if (i == end || *i++ != ' ') {
			return { i, { } };
		}

		auto reasonPhraseResult = ParseReasonPhrase(i, end);
		i = reasonPhraseResult.first;

		if (i == end || *i++ != '\r') {
			return { i, { } };
		}
		if (i == end || *i++ != '\n') {
			return { i, { } };
		}

		return { i, HttpStatus {
			httpVersionResult.second,
			statusCodeResult.second,
			std::move(reasonPhraseResult.second)
		} };
	}

	// RFC 7230, 4.1. Chunked Transfer Coding
	template <typename T, class Iterator, class = typename std::enable_if<std::is_unsigned<T>::value>::type>
	T StringToUint(const Iterator begin, const Iterator end)
	{
		T result = 0;
		for (auto i = begin; i != end; ++i) {
			result = T(10U) * result + DigitToUint<T>(*i);
		}
		return result;
	}

	template <typename T, class Iterator, class = typename std::enable_if<std::is_unsigned<T>::value>::type>
	T HexStringToUint(const Iterator begin, const Iterator end)
	{
		T result = 0;
		for (auto i = begin; i != end; ++i) {
			result = T(16U) * result + HexDigitToUint<T>(*i);
		}
		return result;
	}

	// RFC 7230, 3.1.1. Request Line
	inline std::string EncodeRequestLine(const Containers::StringView& method, const Containers::StringView& target)
	{
		return method + " " + target + " HTTP/1.1\r\n";
	}

	// RFC 7230, 3.2. Header Fields
	inline std::string EncodeHeaderFields(const Containers::SmallVectorImpl<HeaderField>& headerFields)
	{
		std::string result;
		for (const auto& headerField : headerFields) {
			if (headerField.first.empty()) {
				continue;
			}

			result += headerField.first;
			result += ": ";
			result += headerField.second;
			result += "\r\n";
		}

		return result;
	}

	// RFC 4648, 4. Base 64 Encoding
	template <class Iterator>
	std::string EncodeBase64(const Iterator begin, const Iterator end)
	{
		constexpr std::array<char, 64> chars {
			'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
			'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
			'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
			'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
			'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
		};

		std::string result;
		std::size_t c = 0;
		std::array<std::uint8_t, 3> charArray;

		for (auto i = begin; i != end; ++i) {
			charArray[c++] = static_cast<std::uint8_t>(*i);
			if (c == 3) {
				result += chars[static_cast<std::uint8_t>((charArray[0] & 0xFC) >> 2)];
				result += chars[static_cast<std::uint8_t>(((charArray[0] & 0x03) << 4) + ((charArray[1] & 0xF0) >> 4))];
				result += chars[static_cast<std::uint8_t>(((charArray[1] & 0x0F) << 2) + ((charArray[2] & 0xC0) >> 6))];
				result += chars[static_cast<std::uint8_t>(charArray[2] & 0x3f)];
				c = 0;
			}
		}

		if (c) {
			result += chars[static_cast<std::uint8_t>((charArray[0] & 0xFC) >> 2)];

			if (c == 1) {
				result += chars[static_cast<std::uint8_t>((charArray[0] & 0x03) << 4)];
			} else { // c == 2
				result += chars[static_cast<std::uint8_t>(((charArray[0] & 0x03) << 4) + ((charArray[1] & 0xF0) >> 4))];
				result += chars[static_cast<std::uint8_t>((charArray[1] & 0x0F) << 2)];
			}

			while (++c < 4) result += '='; // padding
		}

		return result;
	}

	inline void EncodeHtml(const Uri& uri, const Containers::StringView& method, const Containers::ArrayView<uint8_t>& body, const Containers::SmallVectorImpl<HeaderField>& headerFields, Containers::SmallVectorImpl<std::uint8_t>& targetBuffer)
	{
		// RFC 7230, 5.3. Request Target
		const Containers::String requestTarget = uri.Path + (uri.Query.empty() ? Containers::String { } : "?"_s + uri.Query);
		const auto headerData = EncodeRequestLine(method, requestTarget) +
			// RFC 7230, 5.4. Host
			"Host: " + uri.Host + "\r\n" +
			// RFC 7230, 3.3.2. Content-Length
			"Content-Length: " + std::to_string(body.size()) + "\r\n" +
			EncodeHeaderFields(headerFields) +
			"\r\n";

		auto i = targetBuffer.size();
		targetBuffer.resize_for_overwrite(i + headerData.size() + body.size());
		std::memcpy(targetBuffer.data() + i, (uint8_t*)headerData.data(), headerData.size());
		std::memcpy(targetBuffer.data() + i + headerData.size(), (uint8_t*)body.data(), body.size());
	}

	class Request final
	{
	public:
		explicit Request(const Containers::StringView& uriString,
						 const InternetProtocol protocol = InternetProtocol::V4) :
			internetProtocol { protocol },
			uri { ParseUri(uriString.begin(), uriString.end()) }
		{
		}

		Response Send(const Containers::StringView& method = "GET",
					  const std::chrono::milliseconds timeout = std::chrono::milliseconds { -1 })
		{
			Containers::SmallVector<HeaderField, 0> headerFields;
			return Send(method, Containers::ArrayView<uint8_t>(), headerFields, timeout);
		}

		Response Send(const Containers::StringView& method,
					  const Containers::ArrayView<uint8_t>& body,
					  const Containers::SmallVectorImpl<HeaderField>& headerFields,
					  const std::chrono::milliseconds timeout = std::chrono::milliseconds { -1 })
		{
			const auto stopTime = std::chrono::steady_clock::now() + timeout;

			if (uri.Scheme != "http") {
				return { HttpStatus::NotImplemented };
			}

			addrinfo hints = { };
			hints.ai_family = GetAddressFamily(internetProtocol);
			hints.ai_socktype = SOCK_STREAM;

			const char* port = (uri.Port.empty() ? "80" : uri.Port.data());

			addrinfo* info;
			if (getaddrinfo(uri.Host.data(), port, &hints, &info) != 0) {
				return { HttpStatus::ServiceUnavailable };
			}

			const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addressInfo { info, freeaddrinfo };

			Containers::SmallVector<std::uint8_t, 0> requestData;
			EncodeHtml(uri, method, body, headerFields, requestData);

			Socket socket { internetProtocol };

			const auto getRemainingMilliseconds = [](const std::chrono::steady_clock::time_point time) noexcept -> std::int64_t {
				const auto now = std::chrono::steady_clock::now();
				const auto remainingTime = std::chrono::duration_cast<std::chrono::milliseconds>(time - now);
				return (remainingTime.count() > 0 ? remainingTime.count() : 0);
			};

			if (!socket.Connect(addressInfo->ai_addr, static_cast<socklen_t>(addressInfo->ai_addrlen), (timeout.count() >= 0 ? getRemainingMilliseconds(stopTime) : -1))) {
				return { HttpStatus::ServiceUnavailable };
			}

			auto remaining = requestData.size();
			auto sendData = requestData.data();

			while (remaining > 0) {
				const auto size = socket.Send(sendData, remaining, (timeout.count() >= 0 ? getRemainingMilliseconds(stopTime) : -1));
				remaining -= size;
				sendData += size;
			}

			std::array<std::uint8_t, 4096> tempBuffer;
			constexpr std::array<std::uint8_t, 2> crlf = { '\r', '\n' };
			constexpr std::array<std::uint8_t, 4> headerEnd = { '\r', '\n', '\r', '\n' };
			Response response;
			Containers::SmallVector<std::uint8_t, 0> responseData;
			bool parsingBody = false;
			bool contentLengthReceived = false;
			std::size_t contentLength = 0U;
			bool chunkedResponse = false;
			std::size_t expectedChunkSize = 0U;
			bool removeCrlfAfterChunk = false;

			// Read the response
			while (true) {
				const auto size = socket.Recv(tempBuffer.data(), tempBuffer.size(), (timeout.count() >= 0 ? getRemainingMilliseconds(stopTime) : -1));
				if (size == 0) { // Disconnected
					return response;
				}

				responseData.insert(responseData.end(), tempBuffer.begin(), tempBuffer.begin() + size);

				if (!parsingBody) {
					// RFC 7230, 3. Message Format
					// Empty line indicates the end of the header section (RFC 7230, 2.1. Client/Server Messaging)
					const auto endIterator = std::search(responseData.begin(), responseData.end(), headerEnd.cbegin(), headerEnd.cend());
					if (endIterator == responseData.end()) break; // two consecutive CRLFs not found

					const auto headerBeginIterator = responseData.begin();
					const auto headerEndIterator = endIterator + 2;

					auto statusLineResult = ParseStatusLine(headerBeginIterator, headerEndIterator);
					auto i = statusLineResult.first;

					response.Status = std::move(statusLineResult.second);

					while (true) {
						auto headerFieldResult = ParseHeaderField(i, headerEndIterator);
						i = headerFieldResult.first;

						auto fieldName = std::move(headerFieldResult.second.first);
						const auto toLower = [](const char c) noexcept {
							return (c >= 'A' && c <= 'Z') ? c - ('A' - 'a') : c;
						};
						std::transform(fieldName.begin(), fieldName.end(), fieldName.begin(), toLower);

						auto fieldValue = std::move(headerFieldResult.second.second);

						if (fieldName == "transfer-encoding"_s) {
							// RFC 7230, 3.3.1. Transfer-Encoding
							if (fieldValue == "chunked"_s) {
								chunkedResponse = true;
							} else {
								return { };
							}
						} else if (fieldName == "content-length"_s) {
							// RFC 7230, 3.3.2. Content-Length
							contentLength = StringToUint<std::size_t>(fieldValue.cbegin(), fieldValue.cend());
							contentLengthReceived = true;
							response.Body.reserve(contentLength);
						}

						response.HeaderFields.push_back({ std::move(fieldName), std::move(fieldValue) });

						if (i == headerEndIterator) {
							break;
						}
					}

					responseData.erase(responseData.begin(), headerEndIterator + 2);
					parsingBody = true;
				}

				if (parsingBody) {
					// Content-Length must be ignored if Transfer-Encoding is received (RFC 7230, 3.2. Content-Length)
					if (chunkedResponse) {
						// RFC 7230, 4.1. Chunked Transfer Coding
						while (true) {
							if (expectedChunkSize > 0) {
								const auto toWrite = std::min(expectedChunkSize, responseData.size());
								response.Body.insert(response.Body.end(), responseData.begin(), responseData.begin() + static_cast<std::ptrdiff_t>(toWrite));
								responseData.erase(responseData.begin(), responseData.begin() + static_cast<std::ptrdiff_t>(toWrite));
								expectedChunkSize -= toWrite;

								if (expectedChunkSize == 0) removeCrlfAfterChunk = true;
								if (responseData.empty()) break;
							} else {
								if (removeCrlfAfterChunk) {
									if (responseData.size() < 2) {
										break;
									}
									if (!std::equal(crlf.begin(), crlf.end(), responseData.begin())) {
										return { HttpStatus::ResponseCorrupted };
									}

									removeCrlfAfterChunk = false;
									responseData.erase(responseData.begin(), responseData.begin() + 2);
								}

								const auto i = std::search(responseData.begin(), responseData.end(), crlf.begin(), crlf.end());
								if (i == responseData.end()) break;

								expectedChunkSize = HexStringToUint<std::size_t>(responseData.begin(), i);
								responseData.erase(responseData.begin(), i + 2);

								if (expectedChunkSize == 0) {
									return response;
								}
							}
						}
					} else {
						response.Body.insert(response.Body.end(), responseData.begin(), responseData.end());
						responseData.clear();

						if (contentLengthReceived && response.Body.size() >= contentLength) {
							return response;
						}
					}
				}
			}

			return response;
		}

	private:
#if defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_CYGWIN)
		WinSock winSock;
#endif
		InternetProtocol internetProtocol;
		Uri uri;
	};
}}}

#endif