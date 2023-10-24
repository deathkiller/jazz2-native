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
	std::int32_t copyStringFirst(char* dest, std::int32_t destSize, const char* source, std::int32_t count)
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

	std::int32_t formatString(char* buffer, std::size_t maxLen, const char* format, ...)
	{
		va_list args;
		va_start(args, format);
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
		const std::int32_t writtenChars = vsnprintf_s(buffer, maxLen, maxLen - 1, format, args);
		const std::int32_t result = (writtenChars > -1 ? writtenChars : maxLen - 1);
#else
		const std::int32_t result = ::vsnprintf(buffer, maxLen, format, args);
#endif
		va_end(args);
		return result;
	}

	inline std::uint32_t CountDecimalDigit32(std::uint32_t n)
	{
#if defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_GCC)
		static constexpr std::uint32_t powers_of_10[] = {
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
		std::uint32_t t = (i + 1) * 1233 >> 12;
#	elif defined(DEATH_TARGET_GCC)
		std::uint32_t t = (32 - __builtin_clz(n | 1)) * 1233 >> 12;
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

	inline std::uint32_t CountDecimalDigit64(std::uint64_t n)
	{
#if defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_GCC)
		static constexpr std::uint64_t powers_of_10[] = {
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
		std::uint32_t t = (64 - __builtin_clzll(n | 1)) * 1233 >> 12;
#	elif defined(DEATH_TARGET_32BIT)
		unsigned long i = 0;
		std::uint64_t m = n | 1;
		if (_BitScanReverse(&i, m >> 32)) {
			i += 32;
		} else {
			_BitScanReverse(&i, m & 0xFFFFFFFF);
		}
		std::uint32_t t = (i + 1) * 1233 >> 12;
#	else
		unsigned long i = 0;
		_BitScanReverse64(&i, n | 1);
		std::uint32_t t = (i + 1) * 1233 >> 12;
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

	void u32tos(std::uint32_t value, char* buffer)
	{
		std::uint32_t digit = CountDecimalDigit32(value);
		buffer += digit;
		*buffer = '\0';

		do {
			*--buffer = char(value % 10) + '0';
			value /= 10;
		} while (value > 0);
	}

	void i32tos(std::int32_t value, char* buffer)
	{
		std::uint32_t u = static_cast<std::uint32_t>(value);
		if (value < 0) {
			*buffer++ = '-';
			u = ~u + 1;
		}
		u32tos(u, buffer);
	}

	void u64tos(std::uint64_t value, char* buffer)
	{
		std::uint32_t digit = CountDecimalDigit64(value);
		buffer += digit;
		*buffer = '\0';

		do {
			*--buffer = char(value % 10) + '0';
			value /= 10;
		} while (value > 0);
	}

	void i64tos(std::int64_t value, char* buffer)
	{
		std::uint64_t u = static_cast<std::uint64_t>(value);
		if (value < 0) {
			*buffer++ = '-';
			u = ~u + 1;
		}
		u64tos(u, buffer);
	}

	void ftos(double value, char* buffer, std::int32_t bufferSize)
	{
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
		std::int32_t length = sprintf_s(buffer, bufferSize, "%f", value);
#else
		std::int32_t length = snprintf(buffer, bufferSize, "%f", value);
#endif
		if (length <= 0) {
			buffer[0] = '\0';
			return;
		}

		std::int32_t n = length - 1;
		while (n >= 0 && buffer[n] == '0') {
			n--;
		}
		n++;

		bool separatorFound = false;
		for (std::int32_t i = 0; i < n; i++) {
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
}
