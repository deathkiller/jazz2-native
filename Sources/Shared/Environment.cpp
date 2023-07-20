#include "Environment.h"

#if defined(DEATH_TARGET_SWITCH)
#	include <switch.h>
#elif defined(DEATH_TARGET_WINDOWS_RT)
#	include <winrt/Windows.System.Profile.h>
namespace winrtWSP = winrt::Windows::System::Profile;
#elif defined(DEATH_TARGET_WINDOWS)
#	pragma comment(lib, "psapi")
#
#	include <psapi.h>
#	include <strsafe.h>
#elif defined(DEATH_TARGET_UNIX)
#	include <stdio.h>
#	include <cstring>
#endif

namespace Death::Environment
{
#if defined(DEATH_TARGET_APPLE)
	Containers::String GetAppleVersion()
	{
		FILE* fp = ::fopen("/System/Library/CoreServices/SystemVersion.plist", "r");
		if (fp == nullptr) {
			return { };
		}

		Containers::String result;

		// Look for the line containing the macOS version information
		char line[128];
		while (::fgets(line, sizeof(line), fp) != nullptr) {
			if (strstr(line, "<key>ProductVersion</key>") != nullptr) {
				break;
			}
		}

		// Read the macOS version from the corresponding value tag
		if (::fgets(line, sizeof(line), fp) != nullptr) {
			char* versionStart = strchr(line, '>');
			if (versionStart != nullptr) {
				char* versionEnd = strchr(versionStart, '<');
				if (versionEnd != nullptr) {
					result = Containers::String(versionStart + 1, versionEnd - versionStart - 1);
				}
			}
		}

		::fclose(fp);

		return result;
	}
#elif defined(DEATH_TARGET_SWITCH)
	std::uint32_t GetSwitchVersion()
	{
		return hosversionGet();
	}

	bool HasSwitchAtmosphere()
	{
		return hosversionIsAtmosphere();
	}
#elif defined(DEATH_TARGET_UNIX)
	Containers::String GetUnixVersion()
	{
		FILE* fp = ::fopen("/etc/os-release", "r");
		if (fp == nullptr) {
			return { };
		}

		Containers::String result;

		char* line = nullptr;
		std::size_t length = 0;
		ssize_t read;
		while ((read = ::getline(&line, &length, fp)) != -1) {
			if (strncmp(line, "PRETTY_NAME=", sizeof("PRETTY_NAME=") - 1) == 0) {
				char* versionStart = line + sizeof("PRETTY_NAME=") - 1;
				if (versionStart[0] == '"') {
					versionStart++;
				}
				char* versionEnd = line + read - 1;
				while (versionStart <= versionEnd && (versionEnd[0] == '\0' || versionEnd[0] == '\r' || versionEnd[0] == '\n' || versionEnd[0] == '"')) {
					versionEnd--;
				}

				char* versionShortEnd = versionEnd;
				while (true) {
					if (versionStart >= versionShortEnd) {
						versionShortEnd = nullptr;
						break;
					}
					if (versionShortEnd[0] == '(') {
						versionShortEnd--;
						while (versionStart < versionShortEnd && versionShortEnd[0] == ' ') {
							versionShortEnd--;
						}
						break;
					}
					versionShortEnd--;
				}

				if (versionShortEnd != nullptr) {
					versionEnd = versionShortEnd;
				}
				if (versionStart < versionEnd) {
					result = Containers::String(versionStart, versionEnd - versionStart + 1);
				}
				break;
			}
		}

		if (line != nullptr) {
			::free(line);
		}
		::fclose(fp);

		return result;
	}
#elif defined(DEATH_TARGET_WINDOWS_RT)
	static std::uint64_t GetWindowsVersion()
	{
		winrt::hstring versionString = winrtWSP::AnalyticsInfo::VersionInfo().DeviceFamilyVersion();
		wchar_t* versionStringEnd;
		std::uint64_t version = wcstoull(versionString.begin(), &versionStringEnd, 10);

		std::uint64_t major = (version & 0xFFFF000000000000L) >> 48;
		std::uint64_t minor = (version & 0x0000FFFF00000000L) >> 32;
		std::uint64_t build = (version & 0x00000000FFFF0000L) >> 16;

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

	const std::uint64_t WindowsVersion = GetWindowsVersion();
	const DeviceType CurrentDeviceType = GetDeviceType();
#elif defined(DEATH_TARGET_WINDOWS)
	static std::uint64_t GetWindowsVersion()
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

	const std::uint64_t WindowsVersion = GetWindowsVersion();
#endif
}