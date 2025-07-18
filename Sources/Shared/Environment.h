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

#if defined(DEATH_TARGET_EMSCRIPTEN) || defined(DOXYGEN_GENERATING_OUTPUT)
	/**
	 * @brief Returns whether the application is embedded in another application (or in an `<iframe>` element)
	 *
	 * @partialsupport Available only on @ref DEATH_TARGET_EMSCRIPTEN "Emscripten" platform.
	 */
	bool IsEmbedded();
#endif

	/**
	 * @brief Returns whether the application is currently running in a sandboxed environment
	 * 
	 * Returns @cpp true @ce if running on @ref DEATH_TARGET_IOS "iOS", @ref DEATH_TARGET_ANDROID "Android",
	 * as a @ref DEATH_TARGET_APPLE "macOS" app bundle, @ref DEATH_TARGET_WINDOWS_RT "Windows Phone/Store"
	 * application or in a browser through @ref DEATH_TARGET_EMSCRIPTEN "Emscripten", @cpp false @ce otherwise.
	 */
	bool IsSandboxed();
	
#if defined(DEATH_TARGET_APPLE) || defined(DOXYGEN_GENERATING_OUTPUT)
	/**
	 * @brief Returns version of Apple operating system currently running this application
	 *
	 * @partialsupport Available only on @ref DEATH_TARGET_APPLE "Apple" platforms.
	 */
	Containers::String GetAppleVersion();
#endif

#if defined(DEATH_TARGET_SWITCH) || defined(DOXYGEN_GENERATING_OUTPUT)
	/**
	 * @brief Returns version of Nintendo Switch firmware currently running this application
	 *
	 * @partialsupport Available only on @ref DEATH_TARGET_SWITCH "Nintendo Switch" platform.
	 */
	std::uint32_t GetSwitchVersion();

	/**
	 * @brief Returns `true` if this device is running Atmosphère custom firmware
	 *
	 * @partialsupport Available only on @ref DEATH_TARGET_SWITCH "Nintendo Switch" platform.
	 */
	bool HasSwitchAtmosphere();
#endif

#if defined(DEATH_TARGET_UNIX) || defined(DOXYGEN_GENERATING_OUTPUT)
	/**
	 * @brief Returns name and version of Unix system currently running this application
	 *
	 * @partialsupport Available only on @ref DEATH_TARGET_UNIX "Unix" platform.
	 */
	Containers::String GetUnixFlavor();
#endif

#if defined(DEATH_TARGET_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)
	/** @{ @name Properties */

	/**
	 * @brief Version of Windows® operating system currently running this application
	 * 
	 * @partialsupport Available only on @ref DEATH_TARGET_WINDOWS "Windows" platform.
	 */
	extern const std::uint64_t WindowsVersion;

#	if defined(DEATH_TARGET_WINDOWS_RT) || defined(DOXYGEN_GENERATING_OUTPUT)
	/**
	 * @brief Type of device currently running this application
	 *
	 * @partialsupport Available only on @ref DEATH_TARGET_WINDOWS_RT "Windows RT" platform.
	 */
	extern const DeviceType CurrentDeviceType;
#	endif

	/** @} */

	/**
	 * @brief Returns `true` if this application is currently running on Windows® Vista or later
	 * 
	 * @partialsupport Available only on @ref DEATH_TARGET_WINDOWS "Windows" platform.
	 */
	DEATH_ALWAYS_INLINE bool IsWindowsVista() noexcept {
		return WindowsVersion >= 0x06000000000000; // 6.0.0
	}

	/**
	 * @brief Returns `true` if this application is currently running on Windows® 7 or later
	 * 
	 * @partialsupport Available only on @ref DEATH_TARGET_WINDOWS "Windows" platform.
	 */
	DEATH_ALWAYS_INLINE bool IsWindows7() noexcept {
		return WindowsVersion >= 0x06000100000000; // 6.1.0
	}

	/**
	 * @brief Returns `true` if this application is currently running on Windows® 8 or later
	 * 
	 * @partialsupport Available only on @ref DEATH_TARGET_WINDOWS "Windows" platform.
	 */
	DEATH_ALWAYS_INLINE bool IsWindows8() noexcept {
		return WindowsVersion >= 0x06000300000000; // 6.3.0
	}

	/**
	 * @brief Returns `true` if this application is currently running on Windows® 10 or later
	 * 
	 * @partialsupport Available only on @ref DEATH_TARGET_WINDOWS "Windows" platform.
	 */
	DEATH_ALWAYS_INLINE bool IsWindows10() noexcept {
		return WindowsVersion >= 0x0a0000000047ba; // 10.0.18362
	}

	/**
	 * @brief Returns `true` if this application is currently running on Windows® 11 or later
	 * 
	 * @partialsupport Available only on @ref DEATH_TARGET_WINDOWS "Windows" platform.
	 */
	DEATH_ALWAYS_INLINE bool IsWindows11() noexcept {
		return WindowsVersion >= 0x0a0000000055f0; // 10.0.22000
	}
#endif

	/**
	 * @brief Returns the current unbiased interrupt-time count, in units of 100 nanoseconds
	 * 
	 * The unbiased interrupt-time count is monotonic time source that does not include time the system
	 * spends in sleep or hibernation. Falls back to another monotonic time source if not supported.
	 */
	DEATH_ALWAYS_INLINE std::uint64_t QueryUnbiasedInterruptTime() noexcept {
#if defined(DEATH_TARGET_WINDOWS)
		ULONGLONG now = {};
		::QueryUnbiasedInterruptTime(&now);
		return now;
#elif defined(DEATH_TARGET_APPLE)
		return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 100ULL;
#else
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return std::uint64_t(ts.tv_sec) * 10000000ULL + std::uint64_t(ts.tv_nsec) / 100ULL;
#endif
	}

	/**
	 * @brief Returns the current unbiased interrupt-time count, in milliseconds
	 *
	 * The unbiased interrupt-time count is monotonic time source that does not include time the system
	 * spends in sleep or hibernation. Falls back to another monotonic time source if not supported.
	 */
	DEATH_ALWAYS_INLINE std::uint64_t QueryUnbiasedInterruptTimeAsMs() noexcept {
		return QueryUnbiasedInterruptTime() / 10000LL;
	}

	/**
	 * @brief Returns the current coarse interrupt-time count, in milliseconds
	 *
	 * The coarse interrupt-time count is fast, monotonic time source with a resolution typically limited
	 * to 16 milliseconds. Falls back to another monotonic time source if not supported.
	 */
	DEATH_ALWAYS_INLINE std::uint64_t QueryCoarseInterruptTimeAsMs() noexcept {
#if defined(DEATH_TARGET_WINDOWS)
		return ::GetTickCount64();
#elif defined(DEATH_TARGET_APPLE)
		return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) / 1000000ULL;
#elif defined(DEATH_TARGET_SWITCH)
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return std::uint64_t(ts.tv_sec) * 1000ULL + std::uint64_t(ts.tv_nsec) / 1000000ULL;
#else
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
		return std::uint64_t(ts.tv_sec) * 1000ULL + std::uint64_t(ts.tv_nsec) / 1000000ULL;
#endif
	}

}}