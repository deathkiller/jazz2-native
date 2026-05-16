/*
 *  IXSelectInterrupt.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2019 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace ix
{
	/**
		@brief Abstraction for interrupting select/poll system calls.

		Allows waking up a thread blocked on select/poll, used for network event notification.
	*/
	class SelectInterrupt
	{
	public:
		/** @brief Construct a new SelectInterrupt. */
		SelectInterrupt();
		/** @brief Destructor. */
		virtual ~SelectInterrupt();

		/**
		 * @brief Initialize the interrupt object.
		 * @param errorMsg Output error message.
		 * @return True on success.
		 */
		virtual bool init(std::string& errorMsg);

		/**
		 * @brief Notify the interrupt with a value.
		 * @param value Notification value.
		 * @return True on success.
		 */
		virtual bool notify(uint64_t value);
		/** @brief Clear the interrupt. */
		virtual bool clear();
		/** @brief Read the interrupt value. */
		virtual uint64_t read();
		/** @brief Get the file descriptor for the interrupt. */
		virtual int getFd() const;
		/** @brief Get the event handle (platform-specific). */
		virtual void* getEvent() const;

		/** @brief Special code for send request. */
		static const uint64_t kSendRequest;
		/** @brief Special code for close request. */
		static const uint64_t kCloseRequest;
	};

	/** @brief Unique pointer to a @ref SelectInterrupt */
	using SelectInterruptPtr = std::unique_ptr<SelectInterrupt>;
}
