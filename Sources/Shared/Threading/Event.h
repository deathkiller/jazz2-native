#pragma once

#include "../Common.h"
#include "../Environment.h"
#include "Implementation/WaitOnAddress.h"

#include <atomic>

#if defined(DEATH_TARGET_APPLE)
#	include <pthread.h>
#endif

namespace Death { namespace Threading {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

#ifndef DOXYGEN_GENERATING_OUTPUT
	enum class EventType
	{
		AutoReset,
		ManualReset,
	};
#endif

	/** @brief Lightweight event implementation */
	template<EventType Type>
	class Event
	{
	public:
		explicit Event(bool isSignaled = false) noexcept
			: _isSignaled(isSignaled ? 1 : 0)
		{
			if DEATH_LIKELY(Implementation::IsWaitOnAddressSupported()) {
				Implementation::InitializeWaitOnAddress();
			} else {
#if defined(DEATH_TARGET_WINDOWS)
				_fallbackEvent = ::CreateEvent(nullptr, TRUE, isSignaled, nullptr);
#elif !defined(__DEATH_ALWAYS_USE_WAKEONADDRESS)
				pthread_mutex_init(&_mutex, nullptr);
				pthread_cond_init(&_cond, nullptr);
#endif
			}
		}

		~Event() noexcept
		{
			if DEATH_UNLIKELY(!Implementation::IsWaitOnAddressSupported()) {
#if defined(DEATH_TARGET_WINDOWS)
				::CloseHandle(_fallbackEvent);
#elif !defined(__DEATH_ALWAYS_USE_WAKEONADDRESS)
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

		/** @brief Sets the state of the event to nonsignaled, causing threads to block, returns the previous state of the event */
		bool ResetEvent() noexcept
		{
			bool result;
			if DEATH_LIKELY(Implementation::IsWaitOnAddressSupported()) {
				result = !!_isSignaled.exchange(0);
			} else {
#if defined(DEATH_TARGET_WINDOWS)
				result = !!_isSignaled.exchange(0);
				::ResetEvent(_fallbackEvent);
#elif !defined(__DEATH_ALWAYS_USE_WAKEONADDRESS)
				pthread_mutex_lock(&_mutex);
				result = !!_isSignaled;
				_isSignaled = 0L;
				pthread_mutex_unlock(&_mutex);
#else
				result = false;
#endif
			}
			return result;
		}

		/** @brief Sets the state of the event to signaled, allowing one or more waiting threads to proceed */
		void SetEvent() noexcept
		{
			if DEATH_LIKELY(Implementation::IsWaitOnAddressSupported()) {
				// FYI: 'WakeByAddress*' invokes a full memory barrier
				_isSignaled.store(1, std::memory_order_release);

				#pragma warning(suppress: 4127) // Conditional expression is constant
				if constexpr (Type == EventType::AutoReset) {
					Implementation::WakeByAddressSingle(_isSignaled);
				} else {
					Implementation::WakeByAddressAll(_isSignaled);
				}
			} else {
#if defined(DEATH_TARGET_WINDOWS)
				_isSignaled.store(1, std::memory_order_release);
				::SetEvent(_fallbackEvent);
#elif !defined(__DEATH_ALWAYS_USE_WAKEONADDRESS)
				pthread_mutex_lock(&_mutex);
				_isSignaled.store(1, std::memory_order_relaxed);
				if constexpr (Type == EventType::AutoReset) {
					pthread_cond_signal(&_cond);
				} else {
					pthread_cond_broadcast(&_cond);
				}
				pthread_mutex_unlock(&_mutex);
#endif
			}
		}

		/** @brief Checks if the event is currently signaled, this will not reset the event (unlike Win32 auto-reset event objects) */ 
		bool IsSignaled() const noexcept
		{
			return !!_isSignaled.load(std::memory_order_acquire);
		}

		/** @brief Blocks the current thread until the event receives a signal */
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

		/** @overload */
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
				return Implementation::WaitOnAddress(_isSignaled, 0, timeoutMilliseconds);
			} else {
#if defined(DEATH_TARGET_WINDOWS)
				return (::WaitForSingleObject(_fallbackEvent, timeoutMilliseconds) == WAIT_OBJECT_0);
#elif !defined(__DEATH_ALWAYS_USE_WAKEONADDRESS)
				bool result = true;
				if (timeoutMilliseconds == Implementation::Infinite) {
					pthread_mutex_lock(&_mutex);
					while (!_isSignaled) {
						pthread_cond_wait(&_cond, &_mutex);
					}
					pthread_mutex_unlock(&_mutex);
				} else {
					struct timespec ts;
					clock_gettime(CLOCK_REALTIME, &ts);	// pthread supports only CLOCK_REALTIME here, CLOCK_MONOTONIC was added later
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
#else
				return false;
#endif
			}
		}

		std::atomic_int32_t _isSignaled;
#if defined(DEATH_TARGET_WINDOWS)
		HANDLE _fallbackEvent;
#elif !defined(__DEATH_ALWAYS_USE_WAKEONADDRESS)
		pthread_mutex_t _mutex;
		pthread_cond_t _cond;
#endif
	};

	/** @brief Event object that will atomically revert to an unsignaled state anytime a @relativeref{Event<Type>,Wait()} call succeeds */
	using AutoResetEvent = Event<EventType::AutoReset>;

	/** @brief Event object that once signaled remains that way forever, unless @relativeref{Event<Type>,ResetEvent()} is called */
	using ManualResetEvent = Event<EventType::ManualReset>;

}}