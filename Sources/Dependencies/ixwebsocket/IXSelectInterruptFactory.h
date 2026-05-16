/*
 *  IXSelectInterruptFactory.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2019 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include <memory>

namespace ix
{
	class SelectInterrupt;

	/** @brief Unique pointer to a @ref SelectInterrupt */
	using SelectInterruptPtr = std::unique_ptr<SelectInterrupt>;

	/**
		@brief Factory function to create a platform-specific @ref SelectInterrupt instance.
		@return Unique pointer to @ref SelectInterrupt.
	*/
	SelectInterruptPtr createSelectInterrupt();
}
