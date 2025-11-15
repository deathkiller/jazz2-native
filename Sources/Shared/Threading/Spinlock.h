#pragma once

#include <atomic>

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

		void lock() noexcept
		{
			do {
				while (_state.load(std::memory_order_relaxed) == State::Locked) {
					// Keep trying...
				}
			} while (_state.exchange(State::Locked, std::memory_order_acquire) == State::Locked);
		}

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