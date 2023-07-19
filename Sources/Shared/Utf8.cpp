#include "Utf8.h"
#include "CommonWindows.h"

namespace Death::Utf8
{
	std::size_t GetLength(const Containers::StringView& text)
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

#if defined(DEATH_TARGET_WINDOWS)

	bool ToUtf16(const char* text, std::int32_t size, wchar_t* outputBuffer, std::int32_t outputBufferSize)
	{
		return (::MultiByteToWideChar(CP_UTF8, 0, text, size, outputBuffer, outputBufferSize) != 0);
	}

	Containers::Array<wchar_t> ToUtf16(const char* text, std::int32_t size)
	{
		// MBtoWC counts the trailing \0 into the size, which we have to cut. It also can't be called with a zero
		// size for some stupid reason, in that case just set the result size to zero. We can't just `return {}`,
		// because the output array is guaranteed to be a pointer to a null-terminated string.
		const std::size_t lengthNeeded = (size == 0 ? 0 : ::MultiByteToWideChar(CP_UTF8, 0, text, size, nullptr, 0) - (size == -1 ? 1 : 0));

		// Create the array with a sentinel null terminator. If size is zero, this is just a single null terminator.
		Containers::Array<wchar_t> result { Containers::NoInit, lengthNeeded + 1 };
		result[lengthNeeded] = L'\0';

		if (size != 0) ::MultiByteToWideChar(CP_UTF8, 0, text, size, result.data(), (std::int32_t)lengthNeeded);
		// Return the size without the null terminator
		return Containers::Array<wchar_t>(result.release(), lengthNeeded);
	}

	bool FromUtf16(const wchar_t* text, std::int32_t size, char* outputBuffer, std::int32_t outputBufferSize)
	{
		return (::WideCharToMultiByte(CP_UTF8, 0, text, size, outputBuffer, outputBufferSize, NULL, NULL) != 0);
	}

	Containers::String FromUtf16(const wchar_t* text, std::int32_t size)
	{
		if (size == 0) return { };

		// WCtoMB counts the trailing \0 into the size, which we have to cut. Containers::String takes
		// care of allocating extra for the null terminator so we don't need to do that explicitly.
		Containers::String result { Containers::NoInit, std::size_t(WideCharToMultiByte(CP_UTF8, 0, text, size, nullptr, 0, nullptr, nullptr) - (size == -1 ? 1 : 0)) };
		::WideCharToMultiByte(CP_UTF8, 0, text, size, result.data(), (std::int32_t)result.size(), nullptr, nullptr);
		return result;
	}

#endif
}