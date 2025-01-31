#pragma once

/** @file
	@brief Namespace @ref Death::Environment
*/

#include "CommonWindows.h"

#if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
#	include "Containers/String.h"
#endif

#if !defined(DEATH_TARGET_WINDOWS)
#	include <time.h>
#endif

namespace Death {
#if defined(DEATH_TARGET_WINDOWS_RT)
	enum class DeviceType {
		Unknown,
		Desktop,
		Mobile,
		Iot,
		Xbox
	};
#endif
}

namespace Death { namespace Environment {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
	 * @brief Returns whether the application is running in a sandboxed environment
	 */
	bool IsSandboxed();
	
#if defined(DEATH_TARGET_APPLE)
	Containers::String GetAppleVersion();
#elif defined(DEATH_TARGET_SWITCH)
	std::uint32_t GetSwitchVersion();
	bool HasSwitchAtmosphere();
#elif defined(DEATH_TARGET_UNIX)
	Containers::String GetUnixVersion();
#elif defined(DEATH_TARGET_WINDOWS)
	extern const std::uint64_t WindowsVersion;

	DEATH_ALWAYS_INLINE bool IsWindowsVista() {
		return WindowsVersion >= 0x06000000000000; // 6.0.0
	}

	DEATH_ALWAYS_INLINE bool IsWindows7() {
		return WindowsVersion >= 0x06000100000000; // 6.1.0
	}

	DEATH_ALWAYS_INLINE bool IsWindows8() {
		return WindowsVersion >= 0x06000300000000; // 6.3.0
	}

	DEATH_ALWAYS_INLINE bool IsWindows10() {
		return WindowsVersion >= 0x0a0000000047ba; // 10.0.18362
	}

	DEATH_ALWAYS_INLINE bool IsWindows11() {
		return WindowsVersion >= 0x0a0000000055f0; // 10.0.22000
	}

#	if defined(DEATH_TARGET_WINDOWS_RT)
	extern const DeviceType CurrentDeviceType;
#	endif
#endif

	/**
	 * @brief Returns the current unbiased interrupt-time count, in units of 100 nanoseconds
	 * 
	 * The unbiased interrupt-time count does not include time the system spends in sleep or hibernation.
	 * Falls back to another monotonic time source if not supported.
	 */
#if defined(DEATH_TARGET_WINDOWS)
	DEATH_ALWAYS_INLINE std::uint64_t QueryUnbiasedInterruptTime() {
		ULONGLONG now = {};
		::QueryUnbiasedInterruptTime(&now);
		return now;
	}
#elif defined(DEATH_TARGET_APPLE)
	DEATH_ALWAYS_INLINE std::uint64_t QueryUnbiasedInterruptTime() {
		return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 100ULL;
	}
#else
	DEATH_ALWAYS_INLINE std::uint64_t QueryUnbiasedInterruptTime() {
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return std::uint64_t(ts.tv_sec) * 10000000ULL + std::uint64_t(ts.tv_nsec) / 100ULL;
	}
#endif

	/**
	 * @brief Returns the current unbiased interrupt-time count, in milliseconds
	 *
	 * The unbiased interrupt-time count does not include time the system spends in sleep or hibernation.
	 * Falls back to another monotonic time source if not supported.
	 */
	DEATH_ALWAYS_INLINE std::uint64_t QueryUnbiasedInterruptTimeAsMs() {
		return QueryUnbiasedInterruptTime() / 10000LL;
	}

}}