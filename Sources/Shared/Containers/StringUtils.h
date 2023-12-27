// Copyright © 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016,
//             2017, 2018, 2019, 2020, 2021, 2022, 2023
//           Vladimír Vondruš <mosra@centrum.cz> and contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#pragma once

#include "../Common.h"
#include "String.h"
#include "StringView.h"

namespace Death { namespace Containers { namespace StringUtils {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	namespace Implementation
	{
		extern void DEATH_CPU_DISPATCHED_DECLARATION(lowercaseInPlace)(char* data, std::size_t size);
		DEATH_CPU_DISPATCHER_DECLARATION(lowercaseInPlace)
		extern void DEATH_CPU_DISPATCHED_DECLARATION(uppercaseInPlace)(char* data, std::size_t size);
		DEATH_CPU_DISPATCHER_DECLARATION(uppercaseInPlace)
		extern bool DEATH_CPU_DISPATCHED_DECLARATION(equalsIgnoreCase)(const char* data1, const char* data2, std::size_t size);
		DEATH_CPU_DISPATCHER_DECLARATION(equalsIgnoreCase)
	}

	/**
		@brief Convert ASCII characters in a string to lowercase, in place

		Replaces any character from `ABCDEFGHIJKLMNOPQRSTUVWXYZ` with a corresponding
		character from `abcdefghijklmnopqrstuvwxyz`. Deliberately supports only ASCII
		as Unicode-aware case conversion is a much more complex topic.
	*/
	inline void lowercaseInPlace(MutableStringView string) {
		Implementation::lowercaseInPlace(string.data(), string.size());
	}

	/**
		@brief Convert ASCII characters in a string to lowercase

		Allocates a copy and replaces any character from `ABCDEFGHIJKLMNOPQRSTUVWXYZ`
		with a corresponding character from `abcdefghijklmnopqrstuvwxyz`. Deliberately
		supports only ASCII as Unicode-aware case conversion is a much more complex
		topic.
	*/
	String lowercase(const StringView string);

	/** @overload

		Compared to @ref lowercase(StringView) is able to perform the
		operation in-place if @p string is owned, transferring the data ownership to
		the returned instance. Makes a owned copy first if not.
	*/
	String lowercase(String string);

	/**
		@brief Convert ASCII characters in a string to uppercase, in place

		Replaces any character from `abcdefghijklmnopqrstuvwxyz` with a corresponding
		character from `ABCDEFGHIJKLMNOPQRSTUVWXYZ`. Deliberately supports only ASCII
		as Unicode-aware case conversion is a much more complex topic.
	*/
	inline void uppercaseInPlace(MutableStringView string) {
		Implementation::uppercaseInPlace(string.data(), string.size());
	}

	/**
		@brief Convert ASCII characters in a string to uppercase, in place

		Allocates a copy and replaces any character from `abcdefghijklmnopqrstuvwxyz`
		with a corresponding character from `ABCDEFGHIJKLMNOPQRSTUVWXYZ`. Deliberately
		supports only ASCII as Unicode-aware case conversion is a much more complex
		topic.
	*/
	String uppercase(const StringView string);

	/** @overload

		Compared to @ref uppercase(StringView) is able to perform the
		operation in-place if @p string is owned, transferring the data ownership to
		the returned instance. Makes a owned copy first if not.
	*/
	String uppercase(String string);

	/**
		@brief Convert characters in a UTF-8 string to lowercase with slower Unicode-aware method

		Allocates a copy and replaces most of the Latin, Greek, Cyrillic, Armenian, Georgian, Cherokee,
		Glagolitic, Coptic, Deseret, Osage, Old Hungarian, Warang Citi, Medefaidrin and Adlam characters.
		Doesn't support locale-specific replacements.
	*/
	String lowercaseUnicode(const StringView string);

	/**
		@brief Convert characters in a UTF-8 string to uppercase with slower Unicode-aware method

		Allocates a copy and replaces most of the Latin, Greek, Cyrillic, Armenian, Georgian, Cherokee,
		Glagolitic, Coptic, Deseret, Osage, Old Hungarian, Warang Citi, Medefaidrin and Adlam characters.
		Doesn't support locale-specific replacements.
	*/
	String uppercaseUnicode(const StringView string);

	/**
		@brief Determine whether two strings have the same value, ignoring case (ASCII characters only)
	*/
	inline bool equalsIgnoreCase(const StringView string1, const StringView string2) {
		std::size_t size1 = string1.size();
		std::size_t size2 = string2.size();
		return (size1 == size2 && Implementation::equalsIgnoreCase(string1.data(), string2.data(), size1));
	}

	/**
		@brief Replace first occurrence in a string

		Returns @p string unmodified if it doesn't contain @p search. Having empty
		@p search causes @p replace to be prepended to @p string.
	*/
	String replaceFirst(const StringView string, const StringView search, const StringView replace);

	/**
		@brief Replace all occurrences in a string

		Returns @p string unmodified if it doesn't contain @p search. Expects that
		@p search is not empty, as that would cause an infinite loop. For substituting
		a single character with another the @ref replaceAll(String, char, char)
		variant is more optimal.
	*/
	String replaceAll(StringView string, const StringView search, const StringView replace);

	/**
		@brief Replace all occurrences of a character in a string with another character

		The @p string is passed through unmodified if it doesn't contain @p search.
		Otherwise the operation is performed in-place if @p string is owned,
		transferring the data ownership to the returned instance. An owned copy is made
		if not. See also @ref replaceAllInPlace() for a variant that operates on string
		views.
	*/
	String replaceAll(String string, char search, char replace);

	/**
		@brief Replace all occurrences of a character in a string with another character in-place
	*/
	void replaceAllInPlace(const MutableStringView string, char search, char replace);

}}}