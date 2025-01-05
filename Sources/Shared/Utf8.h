#pragma once

/** @file
	@brief Namespace @ref Death::Utf8
*/

#include "CommonBase.h"
#include "Containers/Array.h"
#include "Containers/String.h"

namespace Death { namespace Utf8 {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Number of characters in a UTF-8 string
	*/
	std::size_t GetLength(Containers::ArrayView<const char> text);

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

	/**
		@brief Previous UTF-8 character

		Returns a Unicode codepoint of a character before @p cursor and its position. If an error occurs,
		returns position of the previous byte and @cpp 0xffffffffu @ce as the codepoint, it's then up to the caller
		whether it gets treated as a fatal error or if the invalid character is simply skipped or replaced.
	*/
	std::pair<char32_t, std::size_t> PrevChar(Containers::ArrayView<const char> text, std::size_t cursor);

	/** @overload */
	/* To fix ambiguity when passing char array in */
	template<std::size_t size>
	inline std::pair<char32_t, std::size_t> PrevChar(const char(&text)[size], const std::size_t cursor) {
		return PrevChar(Containers::ArrayView<const char>{text, size - 1}, cursor);
	}

	/**
		@brief Converts a UTF-32 character to UTF-8
		@param[in]  character   UTF-32 character to convert
		@param[out] result      Where to put the UTF-8 result

		Returns length of the encoding (1, 2, 3 or 4). If @p character is outside of the UTF-32 range, returns @cpp 0 @ce.
	*/
	std::size_t FromCodePoint(char32_t character, Containers::StaticArrayView<4, char> result);

#if defined(DEATH_TARGET_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)

	/**
		@brief Widens a UTF-8 string to UTF-16 for use with Windows Unicode APIs
		
		Converts a UTF-8 string to a wide-string (UTF-16) representation. The primary purpose of this API
		is easy interaction with Windows Unicode APIs. If the text is not empty, the returned array
		contains a sentinel null terminator (i.e., not counted into its size).
		
		@partialsupport Available only on @ref DEATH_TARGET_WINDOWS "Windows" to be
			used when dealing directly with Windows Unicode APIs.
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
	inline Containers::Array<wchar_t> ToUtf16(const Containers::StringView source) {
		return ToUtf16(source.data(), static_cast<std::int32_t>(source.size()));
	}

	/** @overload */
#ifdef DOXYGEN_GENERATING_OUTPUT
	Containers::Array<wchar_t> ToUtf16(const char* source);
#else
	template<class T, class R = Containers::Array<wchar_t>, class = typename std::enable_if<std::is_same<typename std::decay<T>::type, const char*>::value || std::is_same<typename std::decay<T>::type, char*>::value>::type>
	inline R ToUtf16(T&& source) {
		return ToUtf16(source, -1);
	}
#endif

	/**
		@brief Narrows a UTF-16 string to UTF-8 for use with Windows Unicode APIs
		
		Converts a wide-string (UTF-16) to a UTF-8 representation. The primary purpose is easy
		interaction with Windows Unicode APIs.
		
		@partialsupport Available only on @ref DEATH_TARGET_WINDOWS "Windows" to be
			used when dealing directly with Windows Unicode APIs.
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
	inline Containers::String FromUtf16(const Containers::ArrayView<const wchar_t> source) {
		return FromUtf16(source.data(), static_cast<std::int32_t>(source.size()));
	}

	/** @overload */
#ifdef DOXYGEN_GENERATING_OUTPUT
	Containers::String FromUtf16(const wchar_t* source);
#else
	template<class T, class R = Containers::String, class = typename std::enable_if<std::is_same<typename std::decay<T>::type, const wchar_t*>::value || std::is_same<typename std::decay<T>::type, wchar_t*>::value>::type>
	inline R FromUtf16(T&& source) {
		return FromUtf16(source, -1);
	}
#endif

#endif
}}