#include "Utf8.h"
#include "Common.h"

namespace Death::Utf8
{
#if defined(_WIN32)

	bool ToUtf16(const char* text, int size, wchar_t* outputBuffer, int outputBufferSize)
	{
		return (::MultiByteToWideChar(CP_UTF8, 0, text, size, outputBuffer, outputBufferSize) != 0);
	}

	Containers::Array<wchar_t> ToUtf16(const char* text, int size)
	{
		// MBtoWC counts the trailing \0 into the size, which we have to cut. It also can't be called with a zero size
		// for some stupid reason, in that case just set the result size to zero. We can't just `return {}`,
		// because the output array is guaranteed to be a pointer to a null-terminated string.
		const size_t lengthNeeded = (size == 0 ? 0 : ::MultiByteToWideChar(CP_UTF8, 0, text, size, nullptr, 0) - (size == -1 ? 1 : 0));

		// Create the array with a sentinel null terminator. If size is zero, this is just a single null terminator.
		Containers::Array<wchar_t> result { Containers::NoInit, lengthNeeded + 1 };
		result[lengthNeeded] = L'\0';

		if (size) ::MultiByteToWideChar(CP_UTF8, 0, text, size, result.data(), (int)lengthNeeded);
		// Return the size without the null terminator
		return Containers::Array<wchar_t>(result.release(), lengthNeeded);
	}

	bool FromUtf16(const wchar_t* text, int size, char* outputBuffer, int outputBufferSize)
	{
		return (::WideCharToMultiByte(CP_UTF8, 0, text, size, outputBuffer, outputBufferSize, NULL, NULL) != 0);
	}

	Containers::String FromUtf16(const wchar_t* text, int size)
	{
		if (!size) return { };

		// WCtoMB counts the trailing \0 into the size, which we have to cut. Containers::String takes care of allocating
		// extra for the null terminator so we don't need to do that explicitly.
		Containers::String result { Containers::NoInit, std::size_t(WideCharToMultiByte(CP_UTF8, 0, text, size, nullptr, 0, nullptr, nullptr) - (size == -1 ? 1 : 0)) };
		WideCharToMultiByte(CP_UTF8, 0, text, size, result.data(), (int)result.size(), nullptr, nullptr);
		return result;
	}

#endif
}