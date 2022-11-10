#include "Environment.h"

#if defined(DEATH_TARGET_WINDOWS_RT)
#	include <winrt/Windows.System.Profile.h>

namespace winrtWSP = winrt::Windows::System::Profile;
#elif defined(DEATH_TARGET_WINDOWS)
#	pragma comment(lib, "psapi")
#
#	include <psapi.h>
#	include <strsafe.h>
#endif

namespace Death::Environment
{
#if defined(DEATH_TARGET_WINDOWS_RT)
	static uint64_t GetWindowsVersion()
	{
		winrt::hstring versionString = winrtWSP::AnalyticsInfo::VersionInfo().DeviceFamilyVersion();
		wchar_t* versionStringEnd;
		uint64_t version = wcstoull(versionString.begin(), &versionStringEnd, 10);

		uint64_t major = (version & 0xFFFF000000000000L) >> 48;
		uint64_t minor = (version & 0x0000FFFF00000000L) >> 32;
		uint64_t build = (version & 0x00000000FFFF0000L) >> 16;

		return build | ((minor & 0xFFFFull) << 32) | ((major & 0xFFFFull) << 48);
	}

	static DeviceType GetDeviceType()
	{
		winrt::hstring deviceFamily = winrtWSP::AnalyticsInfo::VersionInfo().DeviceFamily();
		if (deviceFamily == L"Windows.Desktop") {
			return DeviceType::Desktop;
		} else if (deviceFamily == L"Windows.Mobile" || deviceFamily == L"Windows.Tablet") {
			return DeviceType::Mobile;
		} else if (deviceFamily == L"Windows.Iot") {
			return DeviceType::Iot;
		} else if (deviceFamily == L"Windows.Xbox") {
			return DeviceType::Xbox;
		} else {
			return DeviceType::Unknown;
		}
	}

	const uint64_t WindowsVersion = GetWindowsVersion();
	const DeviceType CurrentDeviceType = GetDeviceType();
#elif defined(DEATH_TARGET_WINDOWS)
	static uint64_t GetWindowsVersion()
	{
		using _RtlGetNtVersionNumbers = void (WINAPI*)(LPDWORD major, LPDWORD minor, LPDWORD build);

		HMODULE hNtdll = ::GetModuleHandle(L"ntdll.dll");
		if (hNtdll == nullptr) {
			return 0;
		}

		_RtlGetNtVersionNumbers RtlGetNtVersionNumbers = (_RtlGetNtVersionNumbers)::GetProcAddress(hNtdll, "RtlGetNtVersionNumbers");
		if (RtlGetNtVersionNumbers == nullptr) {
			return 0;
		}

		DWORD major, minor, build;
		RtlGetNtVersionNumbers(&major, &minor, &build);
		build &= ~0xF0000000;

		return (build & 0xFFFFFFFFull) | ((minor & 0xFFFFull) << 32) | ((major & 0xFFFFull) << 48);
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