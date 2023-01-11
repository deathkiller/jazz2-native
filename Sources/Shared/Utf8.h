#pragma once

#include "CommonBase.h"
#include "Containers/Array.h"
#include "Containers/String.h"

#include <cstddef>
#include <string>

namespace Death::Utf8
{
	std::pair<char32_t, std::size_t> NextChar(Containers::ArrayView<const char> text, std::size_t cursor);

	template<std::size_t size>
	inline std::pair<char32_t, std::size_t> nextChar(const char(&text)[size], const std::size_t cursor) {
		return NextChar(Containers::ArrayView<const char>{text, size - 1}, cursor);
	}

#if defined(DEATH_TARGET_WINDOWS)

	// Widens UTF-8 string to UTF-16
	bool ToUtf16(const char* text, int size, wchar_t* outputBuffer, int outputBufferSize);

	Containers::Array<wchar_t> ToUtf16(const char* text, int size);

	inline Containers::Array<wchar_t> ToUtf16(const Containers::StringView& text) {
		return ToUtf16(text.data(), (int)text.size());
	}

	template<class T, class R = Containers::Array<wchar_t>, class = typename std::enable_if<std::is_same<typename std::decay<T>::type, const char*>::value || std::is_same<typename std::decay<T>::type, char*>::value>::type>
	inline R ToUtf16(T&& text) {
		return ToUtf16(text, -1);
	}

	// Narrows UTF-16 string to UTF-8
	bool FromUtf16(const wchar_t* text, int size, char* outputBuffer, int outputBufferSize);

	Containers::String FromUtf16(const wchar_t* text, int size);

	inline Containers::String FromUtf16(const Containers::ArrayView<const wchar_t>& text) {
		return FromUtf16(text.data(), (int)text.size());
	}

	inline Containers::String FromUtf16(const std::wstring& text) {
		return FromUtf16(text.c_str(), (int)text.size());
	}

	template<class T, class R = Containers::String, class = typename std::enable_if<std::is_same<typename std::decay<T>::type, const wchar_t*>::value || std::is_same<typename std::decay<T>::type, wchar_t*>::value>::type>
	inline R FromUtf16(T&& text) {
		return FromUtf16(text, -1);
	}

#endif
}