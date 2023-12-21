#include "Utf8.h"
#include "CommonWindows.h"

namespace Death { namespace Utf8 {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	std::size_t GetLength(const Containers::StringView text)
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

	std::pair<char32_t, std::size_t> NextChar(const Containers::ArrayView<const char> text, std::size_t cursor)
	{
		DEATH_ASSERT(cursor < text.size(), {}, "Utf8::NextChar(): Cursor out of range");

		std::uint32_t character = text[cursor];
		std::size_t end = cursor;
		std::uint32_t mask;

		// Sequence size
		if (character < 128) {
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

	std::pair<char32_t, std::size_t> PrevChar(const Containers::ArrayView<const char> text, std::size_t cursor)
	{
		DEATH_ASSERT(cursor > 0, {}, "Utf8::PrevChar(): Cursor already at the beginning");

		std::size_t begin;
		std::uint32_t mask;

		if (std::uint32_t(text[cursor - 1]) < 0x80) {
			begin = cursor - 1;
			mask = 0x7f;
		} else if (cursor > 1 && (text[cursor - 1] & 0xc0) == 0x80) {
			if ((text[cursor - 2] & 0xe0) == 0xc0) {
				begin = cursor - 2;
				mask = 0x1f;
			} else if (cursor > 2 && (text[cursor - 2] & 0xc0) == 0x80) {
				if ((text[cursor - 3] & 0xf0) == 0xe0) {
					begin = cursor - 3;
					mask = 0x0f;
				} else if (cursor > 3 && (text[cursor - 3] & 0xc0) == 0x80) {
					if ((text[cursor - 4] & 0xf8) == 0xf0) {
						begin = cursor - 4;
						mask = 0x07;

					// Sequence too short, wrong cursor position or garbage in the sequence
					} else return {U'\xffffffff', cursor - 1};
				} else return {U'\xffffffff', cursor - 1};
			} else return {U'\xffffffff', cursor - 1};
		} else return {U'\xffffffff', cursor - 1};

		char32_t result = text[begin] & mask;
		for (std::size_t i = begin + 1; i != cursor; ++i) {
			result <<= 6;
			result |= (text[i] & 0x3f);
		}

		return {result, begin};
	}

#if defined(DEATH_TARGET_WINDOWS)

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

	Containers::Array<wchar_t> ToUtf16(const char* source, std::int32_t sourceSize)
	{
		// MBtoWC counts the trailing \0 into the size, which we have to cut. It also can't be called with a zero
		// size for some stupid reason, in that case just set the result size to zero. We can't just `return {}`,
		// because the output array is guaranteed to be a pointer to a null-terminated string.
		const std::size_t lengthNeeded = (sourceSize == 0 ? 0 : ::MultiByteToWideChar(CP_UTF8, 0, source, sourceSize, nullptr, 0) - (sourceSize == -1 ? 1 : 0));

		// Create the array with a sentinel null terminator. If size is zero, this is just a single null terminator.
		Containers::Array<wchar_t> result { Containers::NoInit, lengthNeeded + 1 };
		result[lengthNeeded] = L'\0';

		if (sourceSize != 0) ::MultiByteToWideChar(CP_UTF8, 0, source, sourceSize, result.data(), (std::int32_t)lengthNeeded);
		// Return the size without the null terminator
		return Containers::Array<wchar_t>(result.release(), lengthNeeded);
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

	Containers::String FromUtf16(const wchar_t* source, std::int32_t sourceSize)
	{
		if (sourceSize == 0) return { };

		// WCtoMB counts the trailing \0 into the size, which we have to cut. Containers::String takes
		// care of allocating extra for the null terminator so we don't need to do that explicitly.
		Containers::String result { Containers::NoInit, std::size_t(::WideCharToMultiByte(CP_UTF8, 0, source, sourceSize, nullptr, 0, nullptr, nullptr) - (sourceSize == -1 ? 1 : 0)) };
		::WideCharToMultiByte(CP_UTF8, 0, source, sourceSize, result.data(), (std::int32_t)result.size(), nullptr, nullptr);
		return result;
	}

#endif
}}