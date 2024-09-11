#pragma once

#include "CommonWindows.h"

#if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
#	include "Containers/String.h"
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

#if defined(DEATH_TARGET_WINDOWS_RT)
	extern const DeviceType CurrentDeviceType;
#endif

	DEATH_ALWAYS_INLINE std::uint64_t QueryUnbiasedInterruptTime() {
		ULONGLONG now { };
		::QueryUnbiasedInterruptTime(&now);
		return now;
	}

	DEATH_ALWAYS_INLINE std::uint64_t QueryUnbiasedInterruptTimeAsMs() {
		return QueryUnbiasedInterruptTime() / 10000LL;
	}
#endif
}}