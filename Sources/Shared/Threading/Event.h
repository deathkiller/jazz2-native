#pragma once

#include "../Common.h"
#include "Interlocked.h"
#include "Implementation/WaitOnAddress.h"

namespace Death { namespace Threading {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	enum class EventType
	{
		AutoReset,
		ManualReset,
	};

	template<EventType Type>
	class Event
	{
	public:
		Event(bool isSignaled = false) noexcept
			: _isSignaled(isSignaled ? 1L : 0L)
		{
			if DEATH_LIKELY(Implementation::IsWaitOnAddressSupported()) {
				Implementation::InitializeWaitOnAddress();
			} else {
#if defined(DEATH_TARGET_WINDOWS)
				_event = ::CreateEvent(nullptr, TRUE, isSignaled, nullptr);
				DEATH_DEBUG_ASSERT(_event != NULL);
#endif
			}
		}

		~Event() noexcept
		{
			if DEATH_UNLIKELY(!Implementation::IsWaitOnAddressSupported()) {
#if defined(DEATH_TARGET_WINDOWS)
				::CloseHandle(_event);
				_event = NULL;
#endif
			}
		}

		// Cannot change memory location
		Event(const Event&) = delete;
		Event(Event&&) = delete;
		Event& operator=(const Event&) = delete;
		Event& operator=(Event&&) = delete;

		// Returns the previous state of the event
		bool ResetEvent() noexcept
		{
			LONG result = !!Interlocked::Exchange(&_isSignaled, 0L);
			if DEATH_UNLIKELY(!Implementation::IsWaitOnAddressSupported()) {
#if defined(DEATH_TARGET_WINDOWS)
				::ResetEvent(_event);
#endif
			}
			return result;
		}

		void SetEvent() noexcept
		{
			// FYI: 'WakeByAddress*' invokes a full memory barrier
			Interlocked::WriteRelease(&_isSignaled, 1L);

			if DEATH_LIKELY(Implementation::IsWaitOnAddressSupported()) {
				#pragma warning(suppress: 4127) // Conditional expression is constant
				if constexpr (Type == EventType::AutoReset) {
					Implementation::WakeByAddressSingle(_isSignaled);
				} else {
					Implementation::WakeByAddressAll(_isSignaled);
				}
			} else {
#if defined(DEATH_TARGET_WINDOWS)
				::SetEvent(_event);
#endif
			}
		}

		// Checks if the event is currently signaled
		// Note: Unlike Win32 auto-reset event objects, this will not reset the event
		bool IsSignaled() const noexcept
		{
			return !!Interlocked::ReadAcquire(&_isSignaled);
		}

		bool Wait(std::uint32_t timeoutMilliseconds) noexcept
		{
			if (timeoutMilliseconds == 0) {
				return TryAcquireEvent();
			} else if (timeoutMilliseconds == Implementation::Infinite) {
				return Wait();
			}

			std::uint64_t startTime = Environment::QueryUnbiasedInterruptTime();
			std::uint64_t elapsedMilliseconds = 0;

			while (!TryAcquireEvent()) {
				if (elapsedMilliseconds >= timeoutMilliseconds) {
					return false;
				}

				DWORD newTimeout = static_cast<DWORD>(timeoutMilliseconds - elapsedMilliseconds);
				if (!WaitForSignal(newTimeout)) {
					return false;
				}

				std::uint64_t currTime = Environment::QueryUnbiasedInterruptTime();
				elapsedMilliseconds = (currTime - startTime) / (10 * 1000);
			}

			return true;
		}

		bool Wait() noexcept
		{
			while (!TryAcquireEvent()) {
				if (!WaitForSignal(Implementation::Infinite)) {
					return false;
				}
			}

			return true;
		}

	private:
		bool TryAcquireEvent() noexcept
		{
			#pragma warning(suppress: 4127) // Conditional expression is constant
			if constexpr (Type == EventType::AutoReset) {
				return ResetEvent();
			} else {
				return IsSignaled();
			}
		}

		bool WaitForSignal(std::uint32_t timeoutMilliseconds) noexcept
		{
			if DEATH_LIKELY(Implementation::IsWaitOnAddressSupported()) {
				return Implementation::WaitOnAddress(_isSignaled, 0L, timeoutMilliseconds);
			} else {
#if defined(DEATH_TARGET_WINDOWS)
				return (::WaitForSingleObject(_event, timeoutMilliseconds) == WAIT_OBJECT_0);
#else
				return false;
#endif
			}
		}

		union {
			LONG _isSignaled;
#if defined(DEATH_TARGET_WINDOWS)
			HANDLE _event;
#endif
		};
	};

	/** @brief An event object that will atomically revert to an unsignaled state anytime a `Wait()` call succeeds (i.e. returns true). */
	using AutoResetEvent = Event<EventType::AutoReset>;

	/** @brief An event object that once signaled remains that way forever, unless `ResetEvent()` is called. */
	using ManualResetEvent = Event<EventType::ManualReset>;

}}