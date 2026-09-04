#pragma once

/** @file
	@brief Class @ref Death::Threading::Spinlock
*/

#include "../Common.h"

#include <atomic>

#if defined(DEATH_TARGET_PSP)
#	include <pspthreadman.h>
#endif

namespace Death { namespace Threading {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Lightweight spinlock implementation
	*/
	class Spinlock
	{
	public:
		Spinlock() = default;

		Spinlock(Spinlock const&) = delete;
		Spinlock& operator=(Spinlock const&) = delete;

		/** @brief Acquires the lock, busy-waiting until it becomes available */
		void lock() noexcept
		{
			do {
				while (_state.load(std::memory_order_relaxed) == State::Locked) {
#if defined(DEATH_TARGET_PSP)
					// Single core with no time-slicing: a busy-waiting thread would never let the holder run
					// again, so the waiter sleeps instead (paid only while the lock is contended)
					sceKernelDelayThread(50);
#endif
					// Keep trying...
				}
			} while (_state.exchange(State::Locked, std::memory_order_acquire) == State::Locked);
		}

		/** @brief Releases the lock */
		void unlock() noexcept
		{
			_state.store(State::Free, std::memory_order_release);
		}

	private:
		enum class State : std::uint8_t {
			Free = 0,
			Locked = 1
		};

		std::atomic<State> _state{State::Free};
	};

}}