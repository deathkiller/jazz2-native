#include "Environment.h"

#if defined(DEATH_TARGET_WINDOWS)
#	pragma comment(lib, "psapi")
#
#	include <psapi.h>
#	include <strsafe.h>
#endif

namespace Death
{
#if defined(DEATH_TARGET_WINDOWS)
	static uint64_t GetWindowsVersion()
	{
		using _RtlGetNtVersionNumbers = void (WINAPI*)(LPDWORD major, LPDWORD minor, LPDWORD build);

		_RtlGetNtVersionNumbers RtlGetNtVersionNumbers = nullptr;
		HMODULE hNtdll = ::GetModuleHandle(L"ntdll.dll");
		if (!hNtdll) {
			return 0;
		}

		RtlGetNtVersionNumbers = (_RtlGetNtVersionNumbers)::GetProcAddress(hNtdll, "RtlGetNtVersionNumbers");
		if (!RtlGetNtVersionNumbers) {
			return 0;
		}

		DWORD major, minor, build;
		RtlGetNtVersionNumbers(&major, &minor, &build);
		build &= ~0xF0000000;

		return (build & 0xffffffffull) | ((minor & 0xffffull) << 32) | ((major & 0xffffull) << 48);
	}

	const uint64_t WindowsVersion = GetWindowsVersion();

	bool GetProcessPath(HANDLE hProcess, wchar_t* szFilename, DWORD dwSize)
	{
		if (IsWindowsVista() && ::QueryFullProcessImageName(hProcess, NULL, szFilename, &dwSize)) {
			return true;
		}

		if (::GetModuleFileNameEx(hProcess, NULL, szFilename, dwSize)) {
			return true;
		}

		return false;
	}
#endif
}