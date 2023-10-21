#pragma once

#include "CommonBase.h"
#include "Containers/Array.h"
#include "Containers/String.h"

namespace Death::Utf8
{
	/**
		@brief Number of UTF-8 characters in a string
	*/
	std::size_t GetLength(const Containers::StringView& text);

	/**
		@brief Next UTF-8 character

		Returns Unicode codepoint of character on the cursor and position of the following character.
		If an error occurs, returns position of next byte and @cpp 0xffffffffu @ce as codepoint.
	*/
	std::pair<char32_t, std::size_t> NextChar(Containers::ArrayView<const char> text, std::size_t cursor);

	/** @overload */
	/* To fix ambiguity when passing char array in */
	template<std::size_t size>
	inline std::pair<char32_t, std::size_t> NextChar(const char(&text)[size], const std::size_t cursor) {
		return NextChar(Containers::ArrayView<const char>{text, size - 1}, cursor);
	}

#if defined(DEATH_TARGET_WINDOWS)

	/**
		@brief Widen a UTF-8 string to UTF-16 for use with Windows Unicode APIs
	*/
	std::int32_t ToUtf16(wchar_t* destination, std::int32_t destinationSize, const char* source, std::int32_t sourceSize = -1);

	/** @overload */
	template<std::int32_t size>
	std::int32_t ToUtf16(wchar_t (&destination)[size], const char* source, std::int32_t sourceSize = -1) {
		return ToUtf16(destination, size, source, sourceSize);
	}

	/** @overload */
	Containers::Array<wchar_t> ToUtf16(const char* source, std::int32_t sourceSize);

	/** @overload */
	inline Containers::Array<wchar_t> ToUtf16(const Containers::StringView& source) {
		return ToUtf16(source.data(), static_cast<std::int32_t>(source.size()));
	}

	/** @overload */
	template<class T, class R = Containers::Array<wchar_t>, class = typename std::enable_if<std::is_same<typename std::decay<T>::type, const char*>::value || std::is_same<typename std::decay<T>::type, char*>::value>::type>
	inline R ToUtf16(T&& source) {
		return ToUtf16(source, -1);
	}

	/**
		@brief Narrow a UTF-16 string to UTF-8 for use with Windows Unicode APIs
	*/
	std::int32_t FromUtf16(char* destination, std::int32_t destinationSize, const wchar_t* source, std::int32_t sourceSize = -1);

	/** @overload */
	template<std::int32_t size>
	std::int32_t FromUtf16(char (&destination)[size], const wchar_t* source, std::int32_t sourceSize = -1) {
		return FromUtf16(destination, size, source, sourceSize);
	}

	/** @overload */
	Containers::String FromUtf16(const wchar_t* source, std::int32_t sourceSize);

	/** @overload */
	inline Containers::String FromUtf16(const Containers::ArrayView<const wchar_t>& source) {
		return FromUtf16(source.data(), static_cast<std::int32_t>(source.size()));
	}

	/** @overload */
	template<class T, class R = Containers::String, class = typename std::enable_if<std::is_same<typename std::decay<T>::type, const wchar_t*>::value || std::is_same<typename std::decay<T>::type, wchar_t*>::value>::type>
	inline R FromUtf16(T&& source) {
		return FromUtf16(source, -1);
	}

#endif
}