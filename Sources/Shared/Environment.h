#pragma once

#include "CommonWindows.h"

#if defined(DEATH_TARGET_UNIX)
#	include "Containers/String.h"
#endif

namespace Death
{
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

namespace Death::Environment
{
#if defined(DEATH_TARGET_WINDOWS)
	extern const uint64_t WindowsVersion;

	inline bool IsWindowsVista() {
		return WindowsVersion >= 0x06000000000000; // 6.0.0
	}

	inline bool IsWindows7() {
		return WindowsVersion >= 0x06000100000000; // 6.1.0
	}

	inline bool IsWindows10() {
		return WindowsVersion >= 0x0a0000000047ba; // 10.0.18362
	}

	inline bool IsWindows11() {
		return WindowsVersion >= 0x0a0000000055f0; // 10.0.22000
	}

#if defined(DEATH_TARGET_WINDOWS_RT)
	extern const DeviceType CurrentDeviceType;
#else
	bool GetProcessPath(HANDLE hProcess, wchar_t* szFilename, DWORD dwSize);
#endif

	inline uint64_t QueryUnbiasedInterruptTime()
	{
		ULONGLONG now { };
		::QueryUnbiasedInterruptTime(&now);
		return now;
	}

	inline uint64_t QueryUnbiasedInterruptTimeAsMs()
	{
		return QueryUnbiasedInterruptTime() / 10000LL;
	}
#elif defined(DEATH_TARGET_UNIX)
	Containers::String GetUnixVersion();
#endif
}