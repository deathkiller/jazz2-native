#pragma once

/** @file
	@brief Namespace @ref Death::Environment
*/
/** @namespace Death::Environment
	@brief Platform-specific environment helper functions
*/

#include "CommonWindows.h"

#if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
#	include "Containers/String.h"
#endif

#if !defined(DEATH_TARGET_WINDOWS)
#	include <time.h>
#endif
#if defined(DEATH_TARGET_N64)
#	include <n64sys.h>
#elif defined(DEATH_TARGET_PS3)
#	include <sys/systime.h>
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

	/** @brief Elevation state */
	enum class ElevationState {
		Unknown,			/**< Unknown, the elevation state could not be obtained */
		Limited,			/**< Limited, the process has limited privileges */
		Full				/**< Full, the process has elevated privileges */
	};

	/**
	 * @brief Returns elevation state of the current process
	 */
	ElevationState GetCurrentElevation() noexcept;

#if defined(DEATH_TARGET_EMSCRIPTEN) || defined(DOXYGEN_GENERATING_OUTPUT)
	/**
	 * @brief Returns whether the application is embedded in another application (or in an `<iframe>` element)
	 *
	 * @partialsupport Available only on @ref DEATH_TARGET_EMSCRIPTEN "Emscripten" platform.
	 */
	bool IsEmbedded() noexcept;
#endif

	/**
	 * @brief Returns whether the application is currently running in a sandboxed environment
	 * 
	 * Returns @cpp true @ce if running on @ref DEATH_TARGET_IOS "iOS", @ref DEATH_TARGET_ANDROID "Android",
	 * as a @ref DEATH_TARGET_APPLE "macOS" app bundle, @ref DEATH_TARGET_WINDOWS_RT "Windows Phone/Store"
	 * application or in a browser through @ref DEATH_TARGET_EMSCRIPTEN "Emscripten", @cpp false @ce otherwise.
	 */
	bool IsSandboxed() noexcept;
	
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

	/**
	 * @brief Returns `true` if this application is currently running under Wine compatibility layer
	 *
	 * @partialsupport Available only on @ref DEATH_TARGET_WINDOWS "Windows" platform.
	 */
	bool IsWine() noexcept;
#endif

#if defined(DEATH_TARGET_AMIGAOS)
	namespace Implementation
	{
		// The E-clock read through timer.device, the only fine-grained monotonic source AmigaOS 3.x has
		// (libnix has no monotonic clock_gettime()). Opened on the first call, 0 if it cannot be opened.
		// Raw ticks are for the Amiga backend's frame clock only - everything else uses the queries below.
		std::uint64_t QueryAmigaEClock(std::uint32_t* frequency = nullptr) noexcept;
		std::uint64_t QueryAmigaEClockAsUs() noexcept;
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
#elif defined(DEATH_TARGET_N64)
		// libdragon's newlib has no clock_gettime(), so its monotonic source is the COP0 count register,
		// which get_ticks_us() already extends to 64 bits and converts
		return get_ticks_us() * 10ULL;
#elif defined(DEATH_TARGET_AMIGAOS)
		// libnix has no monotonic clock_gettime() either; the E-clock stands in (see the declarations above).
		// Its tick is 1.4 us, so nothing is lost by scaling microseconds up to the 100 ns unit
		return Implementation::QueryAmigaEClockAsUs() * 10ULL;
#elif defined(DEATH_TARGET_PS3)
		// lv2's clock is the only monotonic source here. it reports seconds and nanoseconds separately,
		// so the 100 ns unit this function returns is assembled from both
		std::uint64_t sec = 0, nsec = 0;
		sysGetCurrentTime(&sec, &nsec);
		return sec * 10000000ULL + nsec / 100ULL;
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
#elif defined(DEATH_TARGET_N64)
		// Same source as QueryUnbiasedInterruptTime(), see there
		return get_ticks_ms();
#elif defined(DEATH_TARGET_AMIGAOS)
		// Same source as QueryUnbiasedInterruptTime(), see there
		return Implementation::QueryAmigaEClockAsUs() / 1000ULL;
#elif defined(DEATH_TARGET_PS3)
		std::uint64_t sec = 0, nsec = 0;
		sysGetCurrentTime(&sec, &nsec);
		return sec * 1000ULL + nsec / 1000000ULL;
#elif defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_PS2) || defined(DEATH_TARGET_PSP) || \
		defined(DEATH_TARGET_VITA) || defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_AMIGAOS4) || \
		defined(DEATH_TARGET_MORPHOS)
		// These platforms have no coarse clock (it is a Linux-specific one), so the precise monotonic clock stands in
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