#include "Algorithms.h"

#include <stdarg.h>
#include <cstring>

#if defined(DEATH_TARGET_MSVC)
#	include <intrin.h>
#endif
#if !defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_MINGW)
#	include <cstdio>
#endif

namespace nCine
{
	int copyStringFirst(char* dest, int destSize, const char* source, int count)
	{
		if (destSize == 0) {
			return 0;
		}

		std::int32_t n = 0;
		while (destSize > 0) {
			if (count == 0) {
				*dest = '\0';
				return n;
			}

			*dest = *source;
			if (*dest == '\0') {
				return n;
			}

			destSize--;
			count--;
			dest++;
			source++;
			n++;
		}

		// Not enough space
		dest--;
		n--;
		*dest = '\0';
		return n;
	}

	int formatString(char* buffer, size_t maxLen, const char* format, ...)
	{
		va_list args;
		va_start(args, format);
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
		const int writtenChars = vsnprintf_s(buffer, maxLen, maxLen - 1, format, args);
		const int result = (writtenChars > -1 ? writtenChars : maxLen - 1);
#else
		const int result = ::vsnprintf(buffer, maxLen, format, args);
#endif
		va_end(args);
		return result;
	}

	inline unsigned CountDecimalDigit32(uint32_t n)
	{
#if defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_GCC)
		static constexpr uint32_t powers_of_10[] = {
			0,
			10,
			100,
			1000,
			10000,
			100000,
			1000000,
			10000000,
			100000000,
			1000000000
		};

#	if defined(DEATH_TARGET_MSVC)
		unsigned long i = 0;
		_BitScanReverse(&i, n | 1);
		uint32_t t = (i + 1) * 1233 >> 12;
#	elif defined(DEATH_TARGET_GCC)
		uint32_t t = (32 - __builtin_clz(n | 1)) * 1233 >> 12;
#	endif
		return t - (n < powers_of_10[t]) + 1;
#else
		// Simple pure C++ implementation
		if (n < 10) return 1;
		if (n < 100) return 2;
		if (n < 1000) return 3;
		if (n < 10000) return 4;
		if (n < 100000) return 5;
		if (n < 1000000) return 6;
		if (n < 10000000) return 7;
		if (n < 100000000) return 8;
		if (n < 1000000000) return 9;
		return 10;
#endif
	}

	inline unsigned CountDecimalDigit64(uint64_t n)
	{
#if defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_GCC)
		static constexpr uint64_t powers_of_10[] = {
			0,
			10,
			100,
			1000,
			10000,
			100000,
			1000000,
			10000000,
			100000000,
			1000000000,
			10000000000,
			100000000000,
			1000000000000,
			10000000000000,
			100000000000000,
			1000000000000000,
			10000000000000000,
			100000000000000000,
			1000000000000000000,
			10000000000000000000U
		};

#	if defined(DEATH_TARGET_GCC)
		uint32_t t = (64 - __builtin_clzll(n | 1)) * 1233 >> 12;
#	elif defined(DEATH_TARGET_32BIT)
		unsigned long i = 0;
		uint64_t m = n | 1;
		if (_BitScanReverse(&i, m >> 32)) {
			i += 32;
		} else {
			_BitScanReverse(&i, m & 0xFFFFFFFF);
		}
		uint32_t t = (i + 1) * 1233 >> 12;
#	else
		unsigned long i = 0;
		_BitScanReverse64(&i, n | 1);
		uint32_t t = (i + 1) * 1233 >> 12;
#	endif
		return t - (n < powers_of_10[t]) + 1;
#else
		// Simple pure C++ implementation
		if (n < 10) return 1;
		if (n < 100) return 2;
		if (n < 1000) return 3;
		if (n < 10000) return 4;
		if (n < 100000) return 5;
		if (n < 1000000) return 6;
		if (n < 10000000) return 7;
		if (n < 100000000) return 8;
		if (n < 1000000000) return 9;
		if (n < 10000000000) return 10;
		if (n < 100000000000) return 11;
		if (n < 1000000000000) return 12;
		if (n < 10000000000000) return 13;
		if (n < 100000000000000) return 14;
		if (n < 1000000000000000) return 15;
		if (n < 10000000000000000) return 16;
		if (n < 100000000000000000) return 17;
		if (n < 1000000000000000000) return 18;
		if (n < 10000000000000000000) return 19;
		return 20;
#endif
	}

	void u32tos(uint32_t value, char* buffer)
	{
		unsigned digit = CountDecimalDigit32(value);
		buffer += digit;
		*buffer = '\0';

		do {
			*--buffer = char(value % 10) + '0';
			value /= 10;
		} while (value > 0);
	}

	void i32tos(int32_t value, char* buffer)
	{
		uint32_t u = static_cast<uint32_t>(value);
		if (value < 0) {
			*buffer++ = '-';
			u = ~u + 1;
		}
		u32tos(u, buffer);
	}

	void u64tos(uint64_t value, char* buffer)
	{
		unsigned digit = CountDecimalDigit64(value);
		buffer += digit;
		*buffer = '\0';

		do {
			*--buffer = char(value % 10) + '0';
			value /= 10;
		} while (value > 0);
	}

	void i64tos(int64_t value, char* buffer)
	{
		uint64_t u = static_cast<uint64_t>(value);
		if (value < 0) {
			*buffer++ = '-';
			u = ~u + 1;
		}
		u64tos(u, buffer);
	}

	void ftos(double value, char* buffer, int bufferSize)
	{
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
		int length = sprintf_s(buffer, bufferSize, "%f", value);
#else
		int length = snprintf(buffer, bufferSize, "%f", value);
#endif
		if (length <= 0) {
			buffer[0] = '\0';
			return;
		}

		int n = length - 1;
		while (n >= 0 && buffer[n] == '0') {
			n--;
		}
		n++;

		bool separatorFound = false;
		for (int i = 0; i < n; i++) {
			if (buffer[i] == '.' || buffer[i] == ',') {
				separatorFound = true;
				break;
			}
		}

		if (separatorFound) {
			if (buffer[n - 1] == '.' || buffer[n - 1] == ',') {
				buffer[n] = '0';
				n++;
			}

			buffer[n] = '\0';
		}
	}

	uint64_t parseVersion(const Containers::StringView version)
	{
		auto parts = version.split('.');
		size_t partsCount = parts.size();
		if (partsCount == 0) {
			return 0;
		}

		uint64_t major = 0, minor = 0, patch = 0;
		size_t partSize;
		char stringBuffer[32];

		partSize = std::min(parts[0].size(), sizeof(stringBuffer) - 1);
		std::memcpy(stringBuffer, parts[0].data(), partSize);
		stringBuffer[partSize] = '\0';
		major = strtol(stringBuffer, nullptr, 10);

		if (partsCount >= 2 && !parts[1].empty()) {
			partSize = std::min(parts[1].size(), sizeof(stringBuffer) - 1);
			std::memcpy(stringBuffer, parts[1].data(), partSize);
			stringBuffer[partSize] = '\0';
			minor = strtol(stringBuffer, nullptr, 10);
		}
		if (partsCount >= 3 && !parts[2].empty()) {
			if (parts[2][0] == 'r') {
				// GIT Revision - always the latest
				patch = 0x0FFFFFFFull;
			} else {
				partSize = std::min(parts[2].size(), sizeof(stringBuffer) - 1);
				std::memcpy(stringBuffer, parts[2].data(), partSize);
				stringBuffer[partSize] = '\0';
				patch = strtol(stringBuffer, nullptr, 10);
			}
		}

		return (patch & 0xFFFFFFFFull) | ((minor & 0xFFFFull) << 32) | ((major & 0xFFFFull) << 48);
	}
}
