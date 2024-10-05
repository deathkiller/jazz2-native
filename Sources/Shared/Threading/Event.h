#pragma once

#include "../Common.h"
#include "../Environment.h"
#include "Interlocked.h"
#include "Implementation/WaitOnAddress.h"

#if defined(DEATH_TARGET_APPLE)
#	include <pthread.h>
#endif

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
#elif defined(DEATH_TARGET_APPLE)
				pthread_mutex_init(&_mutex, nullptr);
				pthread_cond_init(&_cond, nullptr);
#endif
			}
		}

		~Event() noexcept
		{
			if DEATH_UNLIKELY(!Implementation::IsWaitOnAddressSupported()) {
#if defined(DEATH_TARGET_WINDOWS)
				::CloseHandle(_event);
#elif defined(DEATH_TARGET_APPLE)
				pthread_mutex_destroy(&_mutex);
				pthread_cond_destroy(&_cond);
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
			bool result;
			if DEATH_LIKELY(Implementation::IsWaitOnAddressSupported()) {
				result = !!Interlocked::Exchange(&_isSignaled, 0L);
			} else {
#if defined(DEATH_TARGET_WINDOWS)
				result = !!Interlocked::Exchange(&_isSignaled, 0L);
				::ResetEvent(_event);
#elif defined(DEATH_TARGET_APPLE)
				pthread_mutex_lock(&_mutex);
				result = !!_isSignaled;
				_isSignaled = 0L;
				pthread_mutex_unlock(&_mutex);
#endif
			}
			return result;
		}

		void SetEvent() noexcept
		{
			if DEATH_LIKELY(Implementation::IsWaitOnAddressSupported()) {
				// FYI: 'WakeByAddress*' invokes a full memory barrier
				Interlocked::WriteRelease(&_isSignaled, 1L);

				#pragma warning(suppress: 4127) // Conditional expression is constant
				if constexpr (Type == EventType::AutoReset) {
					Implementation::WakeByAddressSingle(_isSignaled);
				} else {
					Implementation::WakeByAddressAll(_isSignaled);
				}
			} else {
#if defined(DEATH_TARGET_WINDOWS)
				Interlocked::WriteRelease(&_isSignaled, 1L);
				::SetEvent(_event);
#elif defined(DEATH_TARGET_APPLE)
				pthread_mutex_lock(&_mutex);
				_isSignaled = 1L;
				if constexpr (Type == EventType::AutoReset) {
					pthread_cond_signal(&_cond);
				} else {
					pthread_cond_broadcast(&_cond);
				}
				pthread_mutex_unlock(&_mutex);
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

				std::uint32_t newTimeout = static_cast<std::uint32_t>(timeoutMilliseconds - elapsedMilliseconds);
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
#elif defined(DEATH_TARGET_APPLE)
				bool result = true;
				if (timeoutMilliseconds == Implementation::Infinite) {
					pthread_mutex_lock(&_mutex);
					while (!_isSignaled) {
						pthread_cond_wait(&_cond, &_mutex);
					}
					pthread_mutex_unlock(&_mutex);
				} else {
					struct timespec ts;
					clock_gettime(CLOCK_REALTIME, &ts);
					ts.tv_sec += timeoutMilliseconds / 1000;
					ts.tv_nsec += (timeoutMilliseconds % 1000) * 1000000;

					if (ts.tv_nsec >= 1000000000) {
						ts.tv_sec += ts.tv_nsec / 1000000000;
						ts.tv_nsec %= 1000000000;
					}

					pthread_mutex_lock(&_mutex);
					while (!_isSignaled) {
						if (pthread_cond_timedwait(&_cond, &_mutex, &ts) == ETIMEDOUT) {
							result = false;
							break;
						}
					}
					pthread_mutex_unlock(&_mutex);
				}
				return result;
#endif
			}
		}

		long _isSignaled;
#if defined(DEATH_TARGET_WINDOWS)
		HANDLE _event;
#elif defined(DEATH_TARGET_APPLE)
		pthread_mutex_t _mutex;
		pthread_cond_t _cond;
#endif
	};

	/** @brief An event object that will atomically revert to an unsignaled state anytime a `Wait()` call succeeds (i.e. returns true). */
	using AutoResetEvent = Event<EventType::AutoReset>;

	/** @brief An event object that once signaled remains that way forever, unless `ResetEvent()` is called. */
	using ManualResetEvent = Event<EventType::ManualReset>;

}}