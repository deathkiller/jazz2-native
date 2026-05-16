/*
 *  IXProgressCallback.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2019 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include <functional>
#include <string>

namespace ix
{
	/**
		@brief Callback type for reporting progress of an operation.

		@param current Current progress value.
		@param total Total value for completion.
		@return True to continue, false to cancel.
	*/
	using OnProgressCallback = std::function<bool(int current, int total)>;

	/**
		@brief Callback type for processing a chunk of data.
		@param chunk Data chunk string.
	*/
	using OnChunkCallback = std::function<void(const std::string&)>;
}
