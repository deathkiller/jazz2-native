#pragma once

#include "../../CommonWindows.h"
#include "../../Asserts.h"

#include <atomic>

#if defined(DEATH_TARGET_WINDOWS)
#	include "../../Environment.h"
#elif defined(DEATH_TARGET_APPLE)
#	include <errno.h>
extern "C"
{
	// Source: https://github.com/apple-oss-distributions/xnu/blob/xnu-8792.81.2/bsd/sys/ulock.h
	// Modification: added __attribute((__weak__))
	// Copyright (c) 2015 Apple Inc. All rights reserved.

	__attribute((__weak__))
	extern int __ulock_wait2(uint32_t operation, void* addr, uint64_t value, uint64_t timeout, uint64_t value2);
	__attribute((__weak__))
	extern int __ulock_wake(uint32_t operation, void* addr, uint64_t wake_value);

	/* Operation bits [7, 0] contain the operation code. */
	#define UL_COMPARE_AND_WAIT             1
	#define UL_COMPARE_AND_WAIT_SHARED      3
	#define UL_COMPARE_AND_WAIT64           5
	#define UL_COMPARE_AND_WAIT64_SHARED    6

	/* Operation bits [15, 8] contain the flags for __ulock_wake */
	#define ULF_WAKE_ALL                    0x00000100
	#define ULF_WAKE_THREAD                 0x00000200
	#define ULF_WAKE_ALLOW_NON_OWNER        0x00000400

	/* Operation bits [15, 8] contain the flags for __ulock_wake */
	#define ULF_WAKE_ALL                    0x00000100
	#define ULF_WAKE_THREAD                 0x00000200
	#define ULF_WAKE_ALLOW_NON_OWNER        0x00000400

	/* Operation bits [31, 24] contain the generic flags */
	#define ULF_NO_ERRNO                    0x01000000
}
#elif defined(__FreeBSD__) || defined(__DragonFly__)
// https://man.freebsd.org/cgi/man.cgi?query=_umtx_op
#	include <sys/types.h>
#	include <sys/umtx.h>
#	include <errno.h>
#	include <time.h>
#elif (defined(__linux__) || defined(__linux)) && !defined(__LSB_VERSION__) && !defined(DEATH_TARGET_EMSCRIPTEN)
#	include <linux/futex.h>
#	include <sys/syscall.h>
#	include <errno.h>
#	include <time.h>
#	include <unistd.h>
#endif

/**
	@brief Cross-platform "wait on address" (futex-style) primitives used to build higher-level synchronization

	Provides a uniform wrapper around each platform's address-based blocking mechanism --- `WaitOnAddress` on
	Windows, `__ulock` on Apple, `_umtx_op` on FreeBSD/DragonFly and `futex` on Linux, with a no-op fallback
	elsewhere. Used internally by @ref Death::Threading::Event and @ref Death::Threading::ReadWriteLock and not
	intended to be used directly.
*/
namespace Death { namespace Threading { namespace Implementation {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/** @brief Trait that yields the underlying value type of a possibly-@cpp std::atomic @ce type */
	template <typename T>
	struct RemoveAtomic {
		using type = T;
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	template <typename T>
	struct RemoveAtomic<std::atomic<T>> {
		using type = T;
	};
#endif

#if defined(DEATH_TARGET_WINDOWS)
	using WaitOnAddressDelegate = decltype(::WaitOnAddress);
	using WakeByAddressAllDelegate = decltype(::WakeByAddressAll);
	using WakeByAddressSingleDelegate = decltype(::WakeByAddressSingle);

	extern WaitOnAddressDelegate* _waitOnAddress;
	extern WakeByAddressAllDelegate* _wakeByAddressAll;
	extern WakeByAddressAllDelegate* _wakeByAddressSingle;

	/** @brief Timeout value representing an infinite wait */
	constexpr std::uint32_t Infinite = INFINITE;

	/** @brief Resolves the platform's wait-on-address entry points; must be called before first use */
	void InitializeWaitOnAddress();

	/** @brief Blocks until @p futex changes away from @p expectedValue or the timeout elapses, returns `true` if woken */
	template<typename T>
	inline bool WaitOnAddress(T& futex, typename RemoveAtomic<T>::type expectedValue, std::uint32_t timeoutMilliseconds)
	{
		BOOL waitResult = _waitOnAddress(&futex, &expectedValue, sizeof(T), timeoutMilliseconds);
		DEATH_DEBUG_ASSERT(waitResult || ::GetLastError() == ERROR_TIMEOUT);
		return !!waitResult;
	}

	/** @brief Wakes all threads waiting on the specified address */
	template<typename T>
	inline void WakeByAddressAll(T& futex)
	{
		_wakeByAddressAll(&futex);
	}

	/** @brief Wakes a single thread waiting on the specified address */
	template<typename T>
	inline void WakeByAddressSingle(T& futex)
	{
		_wakeByAddressSingle(&futex);
	}

	/** @brief Returns `true` if the platform supports wait-on-address blocking */
	inline bool IsWaitOnAddressSupported()
	{
		return Environment::IsWindows8();
	}
#elif defined(DEATH_TARGET_APPLE)
	/** @brief Timeout value representing an infinite wait */
	constexpr std::uint32_t Infinite = ~0;

	/** @brief Resolves the platform's wait-on-address entry points; must be called before first use */
	inline void InitializeWaitOnAddress()
	{
	}

	/** @brief Returns the `__ulock` operation code matching the size of the futex variable */
	template<typename T>
	inline std::uint32_t GetBaseOperation(T&)
	{
		static_assert(sizeof(T) >= sizeof(std::uint32_t), "Can only operate on 32-bit or 64-bit variables");

		std::uint32_t operation = ULF_NO_ERRNO;
		if (sizeof(T) == sizeof(std::uint32_t)) {
			operation |= UL_COMPARE_AND_WAIT;
		} else {
			operation |= UL_COMPARE_AND_WAIT64;
		}
		return operation;
	}

	/** @brief Blocks until @p futex changes away from @p expectedValue or the timeout elapses, returns `true` if woken */
	template<typename T>
	inline bool WaitOnAddress(T& futex, typename RemoveAtomic<T>::type expectedValue, std::uint32_t timeoutMilliseconds)
	{
		// Source code inspection shows __ulock_wait2 uses nanoseconds for timeout
		std::uint64_t timeoutNanoseconds = (timeoutMilliseconds == Infinite ? UINT64_MAX : (timeoutMilliseconds * 1000000ull));
		int r = __ulock_wait2(GetBaseOperation(futex), &futex, std::uint64_t(expectedValue), timeoutNanoseconds, 0);
		return (r == 0 || r != -ETIMEDOUT);
	}

	/** @brief Wakes all threads waiting on the specified address */
	template<typename T>
	inline void WakeByAddressAll(T& futex)
	{
		__ulock_wake(GetBaseOperation(futex) | ULF_WAKE_ALL, &futex, 0);
	}

	/** @brief Wakes a single thread waiting on the specified address */
	template<typename T>
	inline void WakeByAddressSingle(T& futex)
	{
		__ulock_wake(GetBaseOperation(futex), &futex, 0);
	}

	/** @brief Returns `true` if the platform supports wait-on-address blocking */
	inline bool IsWaitOnAddressSupported()
	{
		return (__ulock_wake != nullptr && __ulock_wait2 != nullptr);
	}
#elif defined(__FreeBSD__) || defined(__DragonFly__)
#	define __DEATH_ALWAYS_USE_WAKEONADDRESS

	/** @brief Timeout value representing an infinite wait */
	constexpr std::uint32_t Infinite = ~0;

	/** @brief Resolves the platform's wait-on-address entry points; must be called before first use */
	inline void InitializeWaitOnAddress()
	{
	}

	/** @brief Issues the underlying `_umtx_op` wait, optionally with an absolute timeout */
	template <typename T>
	inline int WaitOnAddressInner(T& futex, typename RemoveAtomic<T>::type expectedValue, _umtx_time* tmp = nullptr)
	{
		// FreeBSD UMTX_OP_WAIT does not apply acquire or release memory barriers
		int op = UMTX_OP_WAIT_UINT_PRIVATE;
		if (sizeof(T) > sizeof(std::uint32_t)) {
			op = UMTX_OP_WAIT;  // No _PRIVATE version
		}
		// The timeout is passed in uaddr2, with its size in uaddr
		void* uaddr = reinterpret_cast<void*>(tmp ? sizeof(*tmp) : 0);
		void* uaddr2 = tmp;
		return _umtx_op(&futex, op, (u_long)expectedValue, uaddr, uaddr2);
	}

	/** @brief Blocks until @p futex changes away from @p expectedValue or the timeout elapses, returns `true` if woken */
	template<typename T>
	inline bool WaitOnAddress(T& futex, typename RemoveAtomic<T>::type expectedValue, std::uint32_t timeoutMilliseconds)
	{
		if (timeoutMilliseconds == Infinite) {
			int r = WaitOnAddressInner(futex, expectedValue);
			return (r == 0);
		} else {
			struct _umtx_time tm = {};

			clock_gettime(CLOCK_MONOTONIC, &tm._timeout);
			tm._timeout.tv_sec += timeoutMilliseconds / 1000;
			tm._timeout.tv_nsec += (timeoutMilliseconds % 1000) * 1000000;

			if (tm._timeout.tv_nsec >= 1000000000) {
				tm._timeout.tv_sec += tm._timeout.tv_nsec / 1000000000;
				tm._timeout.tv_nsec %= 1000000000;
			}

			tm._flags = UMTX_ABSTIME;
			tm._clockid = CLOCK_MONOTONIC;
			int r = WaitOnAddressInner(futex, expectedValue, &tm);
			return (r == 0 || errno != ETIMEDOUT);
		}
	}

	/** @brief Wakes all threads waiting on the specified address */
	template<typename T>
	inline void WakeByAddressAll(T& futex)
	{
		_umtx_op(&futex, UMTX_OP_WAKE_PRIVATE, INT32_MAX, nullptr, nullptr);
	}

	/** @brief Wakes a single thread waiting on the specified address */
	template<typename T>
	inline void WakeByAddressSingle(T& futex)
	{
		_umtx_op(&futex, UMTX_OP_WAKE_PRIVATE, 1, nullptr, nullptr);
	}

	/** @brief Returns `true` if the platform supports wait-on-address blocking */
	inline constexpr bool IsWaitOnAddressSupported()
	{
		return true;
	}
#elif (defined(__linux__) || defined(__linux)) && !defined(__LSB_VERSION__) && !defined(DEATH_TARGET_EMSCRIPTEN)
#	define __DEATH_ALWAYS_USE_WAKEONADDRESS

	/** @brief Timeout value representing an infinite wait */
	constexpr std::uint32_t Infinite = ~0;

	/** @brief Resolves the platform's wait-on-address entry points; must be called before first use */
	inline void InitializeWaitOnAddress()
	{
	}

	/** @brief Thin wrapper around the private `futex` syscall */
	inline long FutexOp(int* addr, int op, int val, std::uintptr_t val2 = 0, int* addr2 = nullptr, int val3 = 0) noexcept
	{
		// We use __NR_futex because some libcs (like Android's bionic) don't provide SYS_futex
		return syscall(__NR_futex, addr, op | FUTEX_PRIVATE_FLAG, val, val2, addr2, val3);
	}

	/** @brief Returns the 32-bit-aligned futex address for @p ptr, accounting for endianness */
	template<typename T>
	int* GetFutexAddress(T* ptr)
	{
		int* intPtr = reinterpret_cast<int*>(ptr);
#	if defined(DEATH_TARGET_BIG_ENDIAN)
		if (sizeof(T) > sizeof(int)) {
			intPtr++; // We want a pointer to the least significant half
		}
#	endif
		return intPtr;
	}

	/** @brief Blocks until @p futex changes away from @p expectedValue or the timeout elapses, returns `true` if woken */
	template<typename T>
	inline bool WaitOnAddress(T& futex, typename RemoveAtomic<T>::type expectedValue, std::uint32_t timeoutMilliseconds)
	{
		if (timeoutMilliseconds == Infinite) {
			long r = FutexOp(GetFutexAddress(&futex), FUTEX_WAIT, (std::uintptr_t)expectedValue);
			return (r == 0);
		} else {
			struct timespec ts;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			ts.tv_sec += timeoutMilliseconds / 1000;
			ts.tv_nsec += (timeoutMilliseconds % 1000) * 1000000;

			if (ts.tv_nsec >= 1000000000) {
				ts.tv_sec += ts.tv_nsec / 1000000000;
				ts.tv_nsec %= 1000000000;
			}

			long r = FutexOp(GetFutexAddress(&futex), FUTEX_WAIT_BITSET, (std::uintptr_t)expectedValue, (std::uintptr_t)&ts, nullptr, FUTEX_BITSET_MATCH_ANY);
			return (r == 0 || errno != ETIMEDOUT);
		}
	}

	/** @brief Wakes all threads waiting on the specified address */
	template<typename T>
	inline void WakeByAddressAll(T& futex)
	{
		FutexOp(GetFutexAddress(&futex), FUTEX_WAKE, INT32_MAX);
	}

	/** @brief Wakes a single thread waiting on the specified address */
	template<typename T>
	inline void WakeByAddressSingle(T& futex)
	{
		FutexOp(GetFutexAddress(&futex), FUTEX_WAKE, 1);
	}

	/** @brief Returns `true` if the platform supports wait-on-address blocking */
	inline constexpr bool IsWaitOnAddressSupported()
	{
		return true;
	}
#else
	/** @brief Timeout value representing an infinite wait */
	constexpr std::uint32_t Infinite = ~0;

	/** @brief Resolves the platform's wait-on-address entry points; must be called before first use */
	inline void InitializeWaitOnAddress()
	{
	}

	/** @brief Blocks until @p futex changes away from @p expectedValue or the timeout elapses, returns `true` if woken (unsupported on this platform, always returns `false`) */
	template<typename T>
	inline bool WaitOnAddress(T& futex, typename RemoveAtomic<T>::type expectedValue, std::uint32_t timeoutMilliseconds)
	{
		return false;
	}

	/** @brief Wakes all threads waiting on the specified address (no-op on this platform) */
	template<typename T>
	inline void WakeByAddressAll(T& futex)
	{
	}

	/** @brief Wakes a single thread waiting on the specified address (no-op on this platform) */
	template<typename T>
	inline void WakeByAddressSingle(T& futex)
	{
	}

	/** @brief Returns `true` if the platform supports wait-on-address blocking */
	inline constexpr bool IsWaitOnAddressSupported()
	{
		return false;
	}
#endif

}}}