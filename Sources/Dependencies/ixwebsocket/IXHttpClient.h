/*
 *  IXHttpClient.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2019 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include "IXHttp.h"
#include "IXSocket.h"
#include "IXSocketTLSOptions.h"
#include "IXWebSocketHttpHeaders.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace ix
{
	/**
		@brief HTTP client for synchronous and asynchronous HTTP requests.
	*/
	class HttpClient
	{
	public:
		/**
		 * @brief Construct a new HttpClient.
		 * @param async Whether to use asynchronous mode.
		 */
		HttpClient(bool async = false);

		/**
		 * @brief Destructor.
		 */
		~HttpClient();

		/**
		 * @brief Perform a synchronous HTTP GET request.
		 */
		HttpResponsePtr get(const std::string& url, HttpRequestArgsPtr args);

		/**
		 * @brief Perform a synchronous HTTP HEAD request.
		 */
		HttpResponsePtr head(const std::string& url, HttpRequestArgsPtr args);

		/**
		 * @brief Perform a synchronous HTTP DELETE request.
		 */
		HttpResponsePtr Delete(const std::string& url, HttpRequestArgsPtr args);

		/**
		 * @brief Perform a synchronous HTTP POST request with parameters and form-data.
		 */
		HttpResponsePtr post(const std::string& url,
							 const HttpParameters& httpParameters,
							 const HttpFormDataParameters& httpFormDataParameters,
							 HttpRequestArgsPtr args);

		/**
		 * @brief Perform a synchronous HTTP POST request with a body.
		 */
		HttpResponsePtr post(const std::string& url,
							 const std::string& body,
							 HttpRequestArgsPtr args);

		/**
		 * @brief Perform a synchronous HTTP PUT request with parameters and form-data.
		 */
		HttpResponsePtr put(const std::string& url,
							const HttpParameters& httpParameters,
							const HttpFormDataParameters& httpFormDataParameters,
							HttpRequestArgsPtr args);

		/**
		 * @brief Perform a synchronous HTTP PUT request with a body.
		 */
		HttpResponsePtr put(const std::string& url,
							const std::string& body,
							HttpRequestArgsPtr args);

		/**
		 * @brief Perform a synchronous HTTP PATCH request with parameters and form-data.
		 */
		HttpResponsePtr patch(const std::string& url,
							  const HttpParameters& httpParameters,
							  const HttpFormDataParameters& httpFormDataParameters,
							  HttpRequestArgsPtr args);

		/**
		 * @brief Perform a synchronous HTTP PATCH request with a body.
		 */
		HttpResponsePtr patch(const std::string& url,
							  const std::string& body,
							  HttpRequestArgsPtr args);

		/**
		 * @brief Perform a generic HTTP request with a body.
		 */
		HttpResponsePtr request(const std::string& url,
								const std::string& verb,
								const std::string& body,
								HttpRequestArgsPtr args,
								int redirects = 0);

		/**
		 * @brief Perform a generic HTTP request with parameters and form-data.
		 */
		HttpResponsePtr request(const std::string& url,
								const std::string& verb,
								const HttpParameters& httpParameters,
								const HttpFormDataParameters& httpFormDataParameters,
								HttpRequestArgsPtr args);

		/**
		 * @brief Force sending a body in requests.
		 */
		void setForceBody(bool value);

		/**
		 * @brief Create a new HTTP request arguments object.
		 */
		HttpRequestArgsPtr createRequest(const std::string& url = std::string(),
										 const std::string& verb = HttpClient::kGet);

		/**
		 * @brief Perform an asynchronous HTTP request.
		 */
		bool performRequest(HttpRequestArgsPtr request,
							const OnResponseCallback& onResponseCallback);

		/**
		 * @brief Set TLS options for HTTPS connections.
		 */
		void setTLSOptions(const SocketTLSOptions& tlsOptions);

		/**
		 * @brief Serialize HTTP parameters to a string.
		 */
		std::string serializeHttpParameters(const HttpParameters& httpParameters);

		/**
		 * @brief Serialize HTTP form-data parameters to a string.
		 */
		std::string serializeHttpFormDataParameters(
			const std::string& multipartBoundary,
			const HttpFormDataParameters& httpFormDataParameters,
			const HttpParameters& httpParameters = HttpParameters());

		/**
		 * @brief Generate a multipart boundary string.
		 */
		std::string generateMultipartBoundary();

		/**
		 * @brief URL-encode a string.
		 */
		std::string urlEncode(const std::string& value);

		const static std::string kPost; /**< POST verb. */
		const static std::string kGet; /**< GET verb. */
		const static std::string kHead; /**< HEAD verb. */
		const static std::string kDelete; /**< DELETE verb. */
		const static std::string kPut; /**< PUT verb. */
		const static std::string kPatch; /**< PATCH verb. */

	private:
		/**
		 * @brief Log a message for a request.
		 */
		void log(const std::string& msg, HttpRequestArgsPtr args);

		// Async API background thread runner
		void run();

		bool _async; /**< Whether async mode is enabled. */
		std::queue<std::pair<HttpRequestArgsPtr, OnResponseCallback>> _queue; /**< Request queue. */
		mutable std::mutex _queueMutex; /**< Mutex for queue. */
		std::condition_variable _condition; /**< Condition variable for queue. */
		std::atomic<bool> _stop; /**< Stop flag for async thread. */
		std::thread _thread; /**< Async thread. */

		std::unique_ptr<Socket> _socket; /**< Socket for HTTP requests. */
		std::recursive_mutex _mutex; /**< Mutex for socket access. */

		SocketTLSOptions _tlsOptions; /**< TLS options. */

		bool _forceBody; /**< Force sending body. */
	};
}
