/*
 *  IXCancellationRequest.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2018 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include <atomic>
#include <functional>

namespace ix
{
	/**
		@brief Function type for cancellation requests (returns true if cancelled).

		Used to signal cancellation of asynchronous operations.
	*/
	using CancellationRequest = std::function<bool()>;

	/**
		@brief Create a cancellation request with a timeout.
		@param seconds Timeout in seconds.
		@param requestInitCancellation Atomic flag for cancellation.
		@return @ref CancellationRequest functor.
	*/
	CancellationRequest makeCancellationRequestWithTimeout(
		int seconds, std::atomic<bool>& requestInitCancellation);
}
