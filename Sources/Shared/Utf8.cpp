#include "Utf8.h"
#include "CommonWindows.h"
#include "Asserts.h"

namespace Death { namespace Utf8 {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	std::size_t GetLength(Containers::ArrayView<const char> text)
	{
		std::size_t size = text.size();
		std::size_t result = 0;
		for (std::size_t i = 0; i < size; i++) {
			if ((text[i] & 0xc0) != 0x80) {
				result++;
			}
		}
		return result;
	}

	Containers::Pair<char32_t, std::size_t> NextChar(const Containers::ArrayView<const char> text, std::size_t cursor)
	{
		DEATH_DEBUG_ASSERT(cursor < text.size(), ("Expected cursor to be less than %zu but got %zu", text.size(), cursor), {});

		std::uint32_t character = text[cursor];
		std::size_t end = cursor;
		std::uint32_t mask;

		// Sequence size
		if (character < 0x80) {
			end += 1;
			mask = 0x7f;
		} else if ((character & 0xe0) == 0xc0) {
			end += 2;
			mask = 0x1f;
		} else if ((character & 0xf0) == 0xe0) {
			end += 3;
			mask = 0x0f;
		} else if ((character & 0xf8) == 0xf0) {
			end += 4;
			mask = 0x07;
		} else {
			// Wrong sequence start
			return { U'\xffffffff', cursor + 1 };
		}

		// Unexpected end
		if (text.size() < end) return { U'\xffffffff', cursor + 1 };

		char32_t result = (character & mask);
		for (std::size_t i = cursor + 1; i != end; ++i) {
			// Garbage in the sequence
			if ((text[i] & 0xc0) != 0x80)
				return { U'\xffffffff', cursor + 1 };

			result <<= 6;
			result |= (text[i] & 0x3f);
		}

		return { result, end };
	}

	Containers::Pair<char32_t, std::size_t> PrevChar(const Containers::ArrayView<const char> text, std::size_t cursor)
	{
		DEATH_DEBUG_ASSERT(cursor > 0 && cursor <= text.size(), ("Expected cursor to be greater than 0 and less than or equal to %zu but got %zu", text.size(), cursor), {});

		// If the previous byte is a continuation byte, go back until it isn't, but only up to three
		// bytes -- any longer sequence of continuation bytes would be invalid anyway
		const std::size_t iMax = (cursor < std::size_t{4} ? cursor : std::size_t{4});
		std::size_t i = 1;
		while (i != iMax && (text[cursor - i] & 0xc0) == 0x80)
			++i;

		// Delegate to NextChar() for the actual codepoint calculation and validation. It's also invalid
		// if the next UTF-8 character isn't *exactly* this cursor position.
		const Containers::Pair<char32_t, std::size_t> prev = NextChar(text, cursor - i);
		if (prev.first() == U'\xffffffff' || prev.second() != cursor)
			return { U'\xffffffff', cursor - 1 };

		return { prev.first(), cursor - i };
	}
	
	std::size_t FromCodePoint(char32_t character, Containers::StaticArrayView<4, char> result)
	{
		if (character < U'\x00000080') {
			result[0] = 0x00 | ((character >> 0) & 0x7f);
			return 1;
		}

		if (character < U'\x00000800') {
			result[0] = 0xc0 | ((character >> 6) & 0x1f);
			result[1] = 0x80 | ((character >> 0) & 0x3f);
			return 2;
		}

		if (character < U'\x00010000') {
			result[0] = 0xe0 | ((character >> 12) & 0x0f);
			result[1] = 0x80 | ((character >> 6) & 0x3f);
			result[2] = 0x80 | ((character >> 0) & 0x3f);
			return 3;
		}

		if (character < U'\x00110000') {
			result[0] = 0xf0 | ((character >> 18) & 0x07);
			result[1] = 0x80 | ((character >> 12) & 0x3f);
			result[2] = 0x80 | ((character >> 6) & 0x3f);
			result[3] = 0x80 | ((character >> 0) & 0x3f);
			return 4;
		}

		// Value outside of UTF-32 range
		return 0;
	}

#if defined(DEATH_TARGET_WINDOWS)

	Containers::Array<wchar_t> ToUtf16(const char* source, std::int32_t sourceSize)
	{
		// MBtoWC counts the trailing \0 into the size, which we have to cut. It also can't be called with a zero
		// size for some stupid reason, in that case just set the result size to zero. We can't just `return {}`,
		// because the output array is guaranteed to be a pointer to a null-terminated string.
		const std::size_t lengthNeeded = (sourceSize == 0 ? 0 : ::MultiByteToWideChar(CP_UTF8, 0, source, sourceSize, nullptr, 0) - (sourceSize == -1 ? 1 : 0));

		// Create the array with a sentinel null terminator. If size is zero, this is just a single null terminator.
		Containers::Array<wchar_t> result{Containers::NoInit, lengthNeeded + 1};
		result[lengthNeeded] = L'\0';

		if (sourceSize != 0) ::MultiByteToWideChar(CP_UTF8, 0, source, sourceSize, result.data(), (std::int32_t)lengthNeeded);
		// Return the size without the null terminator
		return Containers::Array<wchar_t>(result.release(), lengthNeeded);
	}

	std::int32_t ToUtf16(wchar_t* destination, std::int32_t destinationSize, const char* source, std::int32_t sourceSize)
	{
		if (sourceSize == 0) return 0;

		std::int32_t length = ::MultiByteToWideChar(CP_UTF8, 0, source, sourceSize, destination, destinationSize);
		if (length > 0 && sourceSize == -1) {
			length--;	// Return the size without the null terminator
		}
		destination[length] = L'\0';
		return length;
	}

	Containers::String FromUtf16(const wchar_t* source, std::int32_t sourceSize)
	{
		if (sourceSize == 0) return {};

		// WCtoMB counts the trailing \0 into the size, which we have to cut. Containers::String takes
		// care of allocating extra for the null terminator so we don't need to do that explicitly.
		Containers::String result{Containers::NoInit, std::size_t(::WideCharToMultiByte(CP_UTF8, 0, source, sourceSize, nullptr, 0, nullptr, nullptr) - (sourceSize == -1 ? 1 : 0))};
		::WideCharToMultiByte(CP_UTF8, 0, source, sourceSize, result.data(), (std::int32_t)result.size(), nullptr, nullptr);
		return result;
	}

	std::int32_t FromUtf16(char* destination, std::int32_t destinationSize, const wchar_t* source, std::int32_t sourceSize)
	{
		if (sourceSize == 0) return 0;

		std::int32_t length = ::WideCharToMultiByte(CP_UTF8, 0, source, sourceSize, destination, destinationSize, NULL, NULL);
		if (length > 0 && sourceSize == -1) {
			length--;	// Return the size without the null terminator
		}
		destination[length] = '\0';
		return length;
	}

#endif

}}