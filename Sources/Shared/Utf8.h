#pragma once

/** @file
	@brief Namespace @ref Death::Utf8
*/

#include "Containers/Array.h"
#include "Containers/Pair.h"
#include "Containers/StaticArray.h"
#include "Containers/String.h"

namespace Death { namespace Utf8 {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/** @{ @name Properties */

	/**
		@brief Lookup table mapping each possible UTF-8 lead byte (`0x00`–`0xFF`)
		       to the expected number of bytes in the encoded UTF-8 sequence
		
		Each entry corresponds to one value:
		- Values `0x00`–`0x7F`: ASCII (single-byte characters)
		- Values `0x80`–`0xBF`: Continuation bytes (not valid as lead bytes)
		- Values `0xC0`–`0xDF`: Start of 2-byte sequences
		- Values `0xE0`–`0xEF`: Start of 3-byte sequences
		- Values `0xF0`–`0xF4`: Start of 4-byte sequences
		- Values `0xF5`–`0xFF`: Invalid in UTF-8 (beyond Unicode range)
		
		This table allows @f$ \mathcal{O}(1) @f$ lookup of the sequence length given the first
		byte of a UTF-8 encoded character.
	*/
	extern const Containers::StaticArray<256, std::uint8_t> BytesOfLead;

	/** @} */

	/**
		@brief Next UTF-8 character

		Returns Unicode codepoint of character on the cursor and position of the following character.
		If the character is invalid, returns @cpp 0xffffffffu @ce as the codepoint and position of
		the next byte, it's then up to the caller whether it gets treated as a fatal error or if
		the invalid character is simply skipped or replaced.
	*/
	Containers::Pair<char32_t, std::size_t> NextChar(Containers::ArrayView<const char> text, std::size_t cursor);

	/** @overload */
	/* To fix ambiguity when passing char array in */
	template<std::size_t size>
	inline Containers::Pair<char32_t, std::size_t> NextChar(const char(&text)[size], const std::size_t cursor) {
		return NextChar(Containers::ArrayView<const char>{text, size - 1}, cursor);
	}

	/**
		@brief Previous UTF-8 character

		Returns a Unicode codepoint of a character before @p cursor and its position. If the
		character is invalid, returns @cpp 0xffffffffu @ce as the codepoint and position of the
		previous byte, it's then up to the caller whether it gets treated as a fatal error or
		if the invalid character is simply skipped or replaced.
	*/
	Containers::Pair<char32_t, std::size_t> PrevChar(Containers::ArrayView<const char> text, std::size_t cursor);

	/** @overload */
	/* To fix ambiguity when passing char array in */
	template<std::size_t size>
	inline Containers::Pair<char32_t, std::size_t> PrevChar(const char(&text)[size], const std::size_t cursor) {
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
		@brief Widens a UTF-8 string to UTF-16 for use with Windows® Unicode APIs
		
		Converts a UTF-8 string to a wide-string (UTF-16) representation. The primary purpose
		of this API is easy interaction with Windows® Unicode APIs, thus the function doesn't
		return @cpp char16_t @ce but rather a @cpp wchar_t @ce. If the text is not empty,
		the returned array contains a sentinel null terminator (i.e., not counted into its size).
		
		@partialsupport Available only on @ref DEATH_TARGET_WINDOWS "Windows" to be
			used when dealing directly with Windows Unicode APIs.
	*/
	Containers::Array<wchar_t> ToUtf16(const char* source, std::int32_t sourceSize);

	/** @overload */
	inline Containers::Array<wchar_t> ToUtf16(Containers::StringView source) {
		return ToUtf16(source.data(), std::int32_t(source.size()));
	}

#ifndef DOXYGEN_GENERATING_OUTPUT
	/**
		@overload

		Expects that @p source is a null-terminated string.
	*/
	template<class T, class R = Containers::Array<wchar_t>, typename std::enable_if<std::is_same<typename std::decay<T>::type, const char*>::value || std::is_same<typename std::decay<T>::type, char*>::value, int>::type = 0>
	inline R ToUtf16(T&& source) {
		return ToUtf16(source, -1);
	}
#endif

	/**
		@overload

		This overload is suitable if the destination memory is already preallocated (e.g.,
		on the stack). The return value represents the number of converted UTF-16 characters.
		The required @p destinationSize is never larger than @p sourceSize. If @p sourceSize
		is not provided, the source string must be null-terminated.
	*/
	std::int32_t ToUtf16(wchar_t* destination, std::int32_t destinationSize, const char* source, std::int32_t sourceSize = -1);

	/**
		@overload

		This overload is suitable if the destination memory is already preallocated (e.g.,
		on the stack). The return value represents the number of converted UTF-16 characters.
		The required @p destinationSize is never larger than @p sourceSize. If @p sourceSize
		is not provided, the source string must be null-terminated.
	*/
	template<std::int32_t size>
	std::int32_t ToUtf16(wchar_t (&destination)[size], const char* source, std::int32_t sourceSize = -1) {
		return ToUtf16(destination, size, source, sourceSize);
	}

	/**
		@brief Narrows a UTF-16 string to UTF-8 for use with Windows® Unicode APIs
		
		Converts a wide-string (UTF-16) to a UTF-8 representation. The primary purpose is easy
		interaction with Windows® Unicode APIs, thus the function doesn't take @cpp char16_t @ce
		but rather a @cpp wchar_t @ce.
		
		@partialsupport Available only on @ref DEATH_TARGET_WINDOWS "Windows" to be
			used when dealing directly with Windows Unicode APIs.
	*/
	Containers::String FromUtf16(const wchar_t* source, std::int32_t sourceSize);

	/** @overload */
	inline Containers::String FromUtf16(Containers::ArrayView<const wchar_t> source) {
		return FromUtf16(source.data(), std::int32_t(source.size()));
	}

#ifndef DOXYGEN_GENERATING_OUTPUT
	/**
		@overload

		Expects that @p source is a null-terminated string.
	*/
	template<class T, class R = Containers::String, typename std::enable_if<std::is_same<typename std::decay<T>::type, const wchar_t*>::value || std::is_same<typename std::decay<T>::type, wchar_t*>::value, int>::type = 0>
	inline R FromUtf16(T&& source) {
		return FromUtf16(source, -1);
	}
#endif

	/**
		@overload

		This overload is suitable if the destination memory is already preallocated (e.g.,
		on the stack). The return value represents the number of converted UTF-8 characters.
		The required @p destinationSize is never larger than 4× @p sourceSize. If @p sourceSize
		is not provided, the source string must be null-terminated.
	*/
	std::int32_t FromUtf16(char* destination, std::int32_t destinationSize, const wchar_t* source, std::int32_t sourceSize = -1);

	/**
		@overload

		This overload is suitable if the destination memory is already preallocated (e.g.,
		on the stack). The return value represents the number of converted UTF-8 characters.
		The required @p destinationSize is never larger than 4× @p sourceSize. If @p sourceSize
		is not provided, the source string must be null-terminated.
	*/
	template<std::int32_t size>
	std::int32_t FromUtf16(char (&destination)[size], const wchar_t* source, std::int32_t sourceSize = -1) {
		return FromUtf16(destination, size, source, sourceSize);
	}

#endif
}}