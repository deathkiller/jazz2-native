#pragma once

#include "Array.h"

#include <cstddef>
#include <string>

namespace Death::Utf8
{
#if defined(_WIN32) || defined(UTF16_REQUIRED)
	bool ToUtf16(const char* text, int size, wchar_t* outputBuffer, int outputBufferSize);

	Array<wchar_t> ToUtf16(const char* text, int size);

	inline Array<wchar_t> ToUtf16(const ArrayView<char>& text) {
		return ToUtf16(text.data(), (int)text.size());
	}

	inline Array<wchar_t> ToUtf16(const std::string& text) {
		return ToUtf16(text.c_str(), (int)text.size());
	}

	template<std::size_t size>
	inline Array<wchar_t> ToUtf16(const char(&text)[size]) {
		return FromUtf16(text, size - 1);
	}

	template<class T, class R = Array<wchar_t>, class = typename std::enable_if<std::is_same<typename std::decay<T>::type, const char*>::value || std::is_same<typename std::decay<T>::type, char*>::value>::type>
	inline R ToUtf16(T&& text) {
		return ToUtf16(text, -1);
	}
#endif

#if defined(_WIN32)

	bool FromUtf16(const wchar_t* text, int size, char* outputBuffer, int outputBufferSize);

	Array<char> FromUtf16(const wchar_t* text, int size);

	inline Array<char> FromUtf16(const ArrayView<wchar_t>& text) {
		return FromUtf16(text.data(), (int)text.size());
	}

	inline Array<char> FromUtf16(const std::wstring& text) {
		return FromUtf16(text.c_str(), (int)text.size());
	}

	template<std::size_t size>
	inline Array<char> FromUtf16(const wchar_t(&text)[size]) {
		return FromUtf16(text, size - 1);
	}

	template<class T, class R = Array<char>, class = typename std::enable_if<std::is_same<typename std::decay<T>::type, const wchar_t*>::value || std::is_same<typename std::decay<T>::type, wchar_t*>::value>::type>
	inline R FromUtf16(T&& text) {
		return FromUtf16(text, -1);
	}

#endif
}