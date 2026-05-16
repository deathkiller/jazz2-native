/*
 *  IXDNSLookup.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2018 Machine Zone, Inc. All rights reserved.
 *
 *  Resolve a hostname+port to a struct addrinfo obtained with getaddrinfo
 *  Does this in a background thread so that it can be cancelled, since
 *  getaddrinfo is a blocking call, and we don't want to block the main thread on Mobile.
 */

#pragma once

#include "IXCancellationRequest.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>

struct addrinfo;

namespace ix
{
	/**
		@brief Performs asynchronous DNS lookups with cancellation support.

		Resolves hostnames to addresses in a background thread, supporting cancellation via @ref CancellationRequest.
	*/
	class DNSLookup : public std::enable_shared_from_this<DNSLookup>
	{
	public:
		/** @brief Shared pointer to addrinfo struct. */
		using AddrInfoPtr = std::shared_ptr<addrinfo>;
		/**
		 * @brief Construct a new DNSLookup.
		 * @param hostname Hostname to resolve.
		 * @param port Port number.
		 * @param wait Optional wait time.
		 */
		DNSLookup(const std::string& hostname, int port, int64_t wait = DNSLookup::kDefaultWait);
		/** @brief Destructor */
		~DNSLookup() = default;

		/**
		 * @brief Resolve the hostname to an address.
		 * @param errMsg Output error message.
		 * @param isCancellationRequested Cancellation request functor.
		 * @param cancellable True if operation can be cancelled.
		 * @return Shared pointer to addrinfo.
		 */
		AddrInfoPtr resolve(std::string& errMsg,
								 const CancellationRequest& isCancellationRequested,
								 bool cancellable = true);

	private:
		/** @brief Resolve with cancellation support. */
		AddrInfoPtr resolveCancellable(std::string& errMsg,
											const CancellationRequest& isCancellationRequested);
		/** @brief Resolve without cancellation support. */
		AddrInfoPtr resolveUnCancellable(std::string& errMsg,
											  const CancellationRequest& isCancellationRequested);

		/** @brief Get addrinfo for hostname and port. */
		AddrInfoPtr getAddrInfo(const std::string& hostname,
											int port,
											std::string& errMsg);

		/** @brief Thread runner for DNS lookup. */
		void run(std::weak_ptr<DNSLookup> self, std::string hostname, int port);

		/** @brief Set error message. */
		void setErrMsg(const std::string& errMsg);
		/** @brief Get error message. */
		const std::string& getErrMsg();

		/** @brief Set resolved address. */
		void setRes(AddrInfoPtr addr);
		/** @brief Get resolved address. */
		AddrInfoPtr getRes();

		std::string _hostname;
		int _port;
		int64_t _wait;
		const static int64_t kDefaultWait;

		AddrInfoPtr _res;
		std::mutex _resMutex;

		std::string _errMsg;
		std::mutex _errMsgMutex;

		std::atomic<bool> _done;
	};
}
