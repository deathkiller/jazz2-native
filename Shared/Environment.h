#pragma once

#include "Common.h"

namespace Death
{
#if defined(DEATH_TARGET_WINDOWS)
    extern const uint64_t WindowsVersion;

    inline bool IsWindowsVista() {
        return WindowsVersion >= 0x06000000000000; // 6.0.0
    }

    inline bool IsWindows10() {
        return WindowsVersion >= 0x0a0000000047ba; // 10.0.18362
    }

    inline bool IsWindows11() {
        return WindowsVersion >= 0x0a0000000055f0; // 10.0.22000
    }

    bool GetProcessPath(HANDLE hProcess, __out wchar_t* szFilename, DWORD dwSize);

    inline uint64_t QueryUnbiasedInterruptTime()
    {
        ULONGLONG now{ };
        ::QueryUnbiasedInterruptTime(&now);
        return now;
    }

    inline uint64_t QueryUnbiasedInterruptTimeAsMs()
    {
        return QueryUnbiasedInterruptTime() / 10000LL;
    }
#endif
}