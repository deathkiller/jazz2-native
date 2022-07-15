#include "Utf8.h"
#include "Common.h"

namespace Death::Utf8
{
#if defined(_WIN32)

	bool ToUtf16(const char* text, int size, wchar_t* outputBuffer, int outputBufferSize)
	{
		return (::MultiByteToWideChar(CP_UTF8, 0, text, size, outputBuffer, outputBufferSize) != 0);
	}

	Array<wchar_t> ToUtf16(const char* text, int size)
	{
		// MBtoWC counts the trailing \0 into the size, which we have to cut. It also can't be called with a zero size
		// for some stupid reason, in that case just set the result size to zero. We can't just `return {}`,
		// because the output array is guaranteed to be a pointer to a null-terminated string.
		const size_t lengthNeeded = (size == 0 ? 0 : ::MultiByteToWideChar(CP_UTF8, 0, text, size, nullptr, 0) - (size == -1 ? 1 : 0));

		// Create the array with a sentinel null terminator. If size is zero, this is just a single null terminator.
		Array<wchar_t> result { NoInit, lengthNeeded + 1 };
		result[lengthNeeded] = L'\0';

		if (size) ::MultiByteToWideChar(CP_UTF8, 0, text, size, result.data(), (int)lengthNeeded);
		// Return the size without the null terminator
		return Array<wchar_t>(result.release(), lengthNeeded);
	}

	bool FromUtf16(const wchar_t* text, int size, char* outputBuffer, int outputBufferSize)
	{
		return (::WideCharToMultiByte(CP_UTF8, 0, text, size, outputBuffer, outputBufferSize, NULL, NULL) != 0);
	}

	Array<char> FromUtf16(const wchar_t* text, int size)
	{
		const size_t lengthNeeded = (size == 0 ? 0 : ::WideCharToMultiByte(CP_UTF8, 0, text, size, nullptr, 0, nullptr, nullptr) - (size == -1 ? 1 : 0));

		// Create the array with a sentinel null terminator. If size is zero, this is just a single null terminator.
		Array<char> result { NoInit, lengthNeeded + 1 };
		result[lengthNeeded] = L'\0';

		if (size) ::WideCharToMultiByte(CP_UTF8, 0, text, size, result.data(), (int)lengthNeeded, nullptr, nullptr);
		// Return the size without the null terminator
		return Array<char>(result.release(), lengthNeeded);
	}

#else

	// Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
	// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
	static constexpr uint32_t UTF8_ACCEPT = 0;
	static constexpr uint32_t UTF8_REJECT = 12;

	static constexpr uint8_t utf8d[] = {
		// The first part of the table maps bytes to character classes that
		// to reduce the size of the transition table and create bitmasks.
		 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
		 7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		 8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
		10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

		// The second part is a transition table that maps a combination
		// of a state of the automaton and a character class to a state.
		 0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
		12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
		12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
		12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
		12,36,12,12,12,12,12,12,12,12,12,12,
	};

	static inline uint32_t Decode(uint32_t* state, uint32_t* codep, uint32_t byte)
	{
		uint32_t type = utf8d[byte];

		*codep = (*state != UTF8_ACCEPT
					? ((byte & 0x3fu) | (*codep << 6))
					: ((0xff >> type) & (byte)));

		*state = utf8d[256 + *state + type];
		return *state;
	}

	static bool ToUtf16(uint8_t* src, size_t srcBytes, wchar_t* dst, size_t dstWords)
	{
		uint8_t* srcActualEnd = src + srcBytes;
		uint8_t* s = src;
		wchar_t* d = dst;
		uint32_t codepoint;
		uint32_t state = 0;

		while (s < srcActualEnd) {
			size_t dstWordsFree = dstWords - (d - dst);
			uint8_t* srcCurrentEnd = s + dstWordsFree - 1;

			if (srcCurrentEnd > srcActualEnd) {
				srcCurrentEnd = srcActualEnd;
			}
			if (srcCurrentEnd <= s) {
				goto TooSmall;
			}

			while (s < srcCurrentEnd) {
				if (Decode(&state, &codepoint, *s++)) {
					continue;
				}

				if (codepoint > 0xffff) {
					*d++ = (uint16_t)(0xD7C0 + (codepoint >> 10));
					*d++ = (uint16_t)(0xDC00 + (codepoint & 0x3FF));
				} else {
					*d++ = (uint16_t)codepoint;
				}
			}
		}

		if (state != UTF8_ACCEPT) {
			return false;
		}

		if ((dstWords - (d - dst)) == 0) {
			goto TooSmall;
		}

		*d++ = L'\0';
		return true;

	TooSmall:
		return false;
	}

	static size_t ToUtf16Length(uint8_t* src, size_t srcBytes)
	{
		uint8_t* srcActualEnd = src + srcBytes;
		uint8_t* s = src;
		uint32_t codepoint;
		uint32_t state = 0;
		size_t length = 0;

		while (s < srcActualEnd) {
			if (Decode(&state, &codepoint, *s++)) {
				continue;
			}

			if (codepoint > 0xffff) {
				length += 2;
			} else {
				length++;
			}
		}

		if (state != UTF8_ACCEPT) {
			return -1;
		}

		// Include terminating NULL
		length++;
		return length;
	}

	bool ToUtf16(const char* text, int size, wchar_t* outputBuffer, int outputBufferSize)
	{
		if (size == -1) {
			size = (int)strnlen_s(text, size);
		}
		const size_t lengthNeeded = (size == 0 ? 0 : ToUtf16Length((uint8_t*)text, size));
		if (lengthNeeded > outputBufferSize) {
			return false;
		}

		if (size) ToUtf16((uint8_t*)text, size, outputBuffer, lengthNeeded);
		return true;
	}

	Array<wchar_t> ToUtf16(const char* text, int size)
	{
		if (size == -1) {
			size = (int)strnlen_s(text, size);
		}
		const size_t lengthNeeded = (size == 0 ? 0 : ToUtf16Length((uint8_t*)text, size));

		Array<wchar_t> result { NoInit, lengthNeeded + 1 };
		result[lengthNeeded] = L'\0';

		if (size) ToUtf16((uint8_t*)text, size, result.data(), lengthNeeded);
		// Return the size without the null terminator
		return Array<wchar_t>(result.release(), lengthNeeded);
	}

#endif
}