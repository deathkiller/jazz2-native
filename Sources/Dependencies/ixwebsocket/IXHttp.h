/*
 * IXHttp.h
 * Author: Benjamin Sergeant
 * Copyright (c) 2019 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include "IXProgressCallback.h"
#include "IXWebSocketHttpHeaders.h"
#include <atomic>
#include <cstdint>
#include <tuple>
#include <unordered_map>

namespace ix
{
	/**
		@brief HTTP error codes for @ref HttpResponse and HTTP operations.
	*/
	enum class HttpErrorCode : int
	{
		Ok = 0, /**< No error. */
		CannotConnect = 1, /**< Cannot connect to server. */
		Timeout = 2, /**< Operation timed out. */
		Gzip = 3, /**< Gzip error. */
		UrlMalformed = 4, /**< Malformed URL. */
		CannotCreateSocket = 5, /**< Could not create socket. */
		SendError = 6, /**< Error sending data. */
		ReadError = 7, /**< Error reading data. */
		CannotReadStatusLine = 8, /**< Could not read status line. */
		MissingStatus = 9, /**< Missing status in response. */
		HeaderParsingError = 10, /**< Error parsing headers. */
		MissingLocation = 11, /**< Missing redirect location. */
		TooManyRedirects = 12, /**< Too many redirects. */
		ChunkReadError = 13, /**< Error reading chunked body. */
		CannotReadBody = 14, /**< Could not read body. */
		Cancelled = 15, /**< Request was cancelled. */
		Invalid = 100 /**< Invalid error code. */
	};

	/**
		@brief HTTP response structure for @ref HttpClient and @ref HttpServer.
	*/
	struct HttpResponse
	{
		int statusCode; /**< HTTP status code. */
		std::string description; /**< Status description. */
		HttpErrorCode errorCode; /**< Error code, see @ref HttpErrorCode. */
		WebSocketHttpHeaders headers; /**< HTTP headers. */
		std::string body; /**< Response body. */
		std::string errorMsg; /**< Error message. */
		uint64_t uploadSize; /**< Uploaded bytes. */
		uint64_t downloadSize; /**< Downloaded bytes. */

		/**
		 * @brief Constructor for @ref HttpResponse.
		 */
		HttpResponse(int s = 0,
					 const std::string& des = std::string(),
					 const HttpErrorCode& c = HttpErrorCode::Ok,
					 const WebSocketHttpHeaders& h = WebSocketHttpHeaders(),
					 const std::string& b = std::string(),
					 const std::string& e = std::string(),
					 uint64_t u = 0,
					 uint64_t d = 0)
			: statusCode(s)
			, description(des)
			, errorCode(c)
			, headers(h)
			, body(b)
			, errorMsg(e)
			, uploadSize(u)
			, downloadSize(d)
		{
		}
	};

	using HttpResponsePtr = std::shared_ptr<HttpResponse>; /**< Pointer to @ref HttpResponse. */
	using HttpParameters = std::unordered_map<std::string, std::string>; /**< HTTP parameters map. */
	using HttpFormDataParameters = std::unordered_map<std::string, std::string>; /**< HTTP form-data parameters map. */
	using Logger = std::function<void(const std::string&)>; /**< Logger callback. */
	using OnResponseCallback = std::function<void(const HttpResponsePtr&)>; /**< Response callback. */

	/**
		@brief Arguments for HTTP requests.
	*/
	struct HttpRequestArgs
	{
		std::string url; /**< Request URL. */
		std::string verb; /**< HTTP verb (GET, POST, etc.). */
		WebSocketHttpHeaders extraHeaders; /**< Extra HTTP headers. */
		std::string body; /**< Request body. */
		std::string multipartBoundary; /**< Multipart boundary for form-data. */
		int connectTimeout = 60; /**< Connection timeout in seconds. */
		int transferTimeout = 1800; /**< Transfer timeout in seconds. */
		bool followRedirects = true; /**< Whether to follow redirects. */
		int maxRedirects = 5; /**< Maximum number of redirects. */
		bool verbose = false; /**< Verbose output. */
		bool compress = true; /**< Enable compression. */
		bool compressRequest = false; /**< Compress request body. */
		Logger logger; /**< Logger callback. */
		OnProgressCallback onProgressCallback; /**< Progress callback. */
		OnChunkCallback onChunkCallback; /**< Chunk callback. */
		std::atomic<bool> cancel; /**< Cancellation flag. */
	};

	using HttpRequestArgsPtr = std::shared_ptr<HttpRequestArgs>; /**< Pointer to @ref HttpRequestArgs. */

	/**
		@brief HTTP request structure for @ref HttpServer and @ref HttpClient.
	*/
	struct HttpRequest
	{
		std::string uri; /**< Request URI. */
		std::string method; /**< HTTP method. */
		std::string version; /**< HTTP version. */
		std::string body; /**< Request body. */
		WebSocketHttpHeaders headers; /**< HTTP headers. */

		/**
		 * @brief Constructor for @ref HttpRequest.
		 */
		HttpRequest(const std::string& u,
					const std::string& m,
					const std::string& v,
					const std::string& b,
					const WebSocketHttpHeaders& h = WebSocketHttpHeaders())
			: uri(u)
			, method(m)
			, version(v)
			, body(b)
			, headers(h)
		{
		}
	};

	using HttpRequestPtr = std::shared_ptr<HttpRequest>; /**< Pointer to @ref HttpRequest. */

	/**
		@brief HTTP protocol utility class for parsing and sending HTTP requests and responses.
	*/
	class Http
	{
	public:
		/**
		 * @brief Parse an HTTP request from a socket.
		 * @param socket The socket to read from.
		 * @param timeoutSecs Timeout in seconds.
		 * @return Tuple of (success, error message, request pointer).
		 */
		static std::tuple<bool, std::string, HttpRequestPtr> parseRequest(
			std::unique_ptr<Socket>& socket, int timeoutSecs);

		/**
		 * @brief Send an HTTP response over a socket.
		 * @param response The response to send.
		 * @param socket The socket to write to.
		 * @return True if successful.
		 */
		static bool sendResponse(HttpResponsePtr response, std::unique_ptr<Socket>& socket);

		/**
		 * @brief Parse the status line from an HTTP response.
		 * @param line The status line.
		 * @return Pair of (status, code).
		 */
		static std::pair<std::string, int> parseStatusLine(const std::string& line);

		/**
		 * @brief Parse the request line from an HTTP request.
		 * @param line The request line.
		 * @return Tuple of (method, URI, version).
		 */
		static std::tuple<std::string, std::string, std::string> parseRequestLine(
			const std::string& line);

		/**
		 * @brief Trim whitespace from a string.
		 * @param str The string to trim.
		 * @return Trimmed string.
		 */
		static std::string trim(const std::string& str);
	};
}
