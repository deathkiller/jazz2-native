#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"

#include <cstddef>
#include <string>

namespace Death::Utf8
{
#if defined(_WIN32)

	bool ToUtf16(const char* text, int size, wchar_t* outputBuffer, int outputBufferSize);

	Containers::Array<wchar_t> ToUtf16(const char* text, int size);

	inline Containers::Array<wchar_t> ToUtf16(const Containers::StringView& text) {
		return ToUtf16(text.data(), (int)text.size());
	}

	inline Containers::Array<wchar_t> ToUtf16(const std::string& text) {
		return ToUtf16(text.c_str(), (int)text.size());
	}

	template<std::size_t size>
	inline Containers::Array<wchar_t> ToUtf16(const char(&text)[size]) {
		return ToUtf16(text, size - 1);
	}

	bool FromUtf16(const wchar_t* text, int size, char* outputBuffer, int outputBufferSize);

	Containers::String FromUtf16(const wchar_t* text, int size);

	inline Containers::String FromUtf16(const Containers::ArrayView<const wchar_t>& text) {
		return FromUtf16(text.data(), (int)text.size());
	}

	inline Containers::String FromUtf16(const std::wstring& text) {
		return FromUtf16(text.c_str(), (int)text.size());
	}

	template<std::size_t size>
	inline Containers::String FromUtf16(const wchar_t(&text)[size]) {
		return FromUtf16(text, size - 1);
	}

#endif
}