/*
 *  IXSelectInterruptEvent.h
 */

#pragma once

#include "IXSelectInterrupt.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <deque>
#ifdef _WIN32
#include <windows.h>
#endif

namespace ix
{
	/**
		@brief Platform-specific implementation of @ref SelectInterrupt using events.

		Provides event-based select/poll interruption for network event notification.
	*/
	class SelectInterruptEvent final : public SelectInterrupt
	{
	public:
		/** @brief Construct a new SelectInterruptEvent. */
		SelectInterruptEvent();
		/** @brief Destructor. */
		virtual ~SelectInterruptEvent();

		/** @brief Initialize the event interrupt. */
		bool init(std::string& /*errorMsg*/) final;
		/** @brief Notify the event interrupt. */
		bool notify(uint64_t value) final;
		/** @brief Clear the event interrupt. */
		bool clear() final;
		/** @brief Read the event interrupt value. */
		uint64_t read() final;
		/** @brief Get the event handle (platform-specific). */
		void* getEvent() const final;
	private:
		/** @brief Queue of interrupt values. */
		std::deque<uint64_t> _values;
		/** @brief Mutex for value queue. */
		std::mutex _valuesMutex;
#ifdef _WIN32
		/** @brief Windows event handle for socket poll wakeup. */
		HANDLE _event;
#endif
	};
}
