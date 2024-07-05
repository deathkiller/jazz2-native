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

/** @file
	@brief STL @ref std::string compatibility for @ref Death::Containers::String and @ref Death::Containers::StringView

	Including this header allows you to convert a
	@ref Death::Containers::String / @ref Death::Containers::StringView from
	and to a @ref std::string.
*/

#include <string>

#include "String.h"
#include "StringView.h"

namespace Death { namespace Containers { namespace Implementation {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/** @brief Provides automatic conversion from/to STL @ref std::string */
	template<> struct StringConverter<std::string> {
		static String from(const std::string& other) {
			return String{other.data(), other.size()};
		}
		static std::string to(const String& other) {
			return std::string{other.data(), other.size()};
		}
	};

	/** @brief Provides automatic conversion from/to STL @ref std::string */
	template<> struct StringViewConverter<const char, std::string> {
		static StringView from(const std::string& other) {
			return StringView{other.data(), other.size(), StringViewFlags::NullTerminated};
		}
		static std::string to(StringView other) {
			return std::string{other.data(), other.size()};
		}
	};

	/** @brief Provides automatic conversion from/to STL @ref std::string */
	template<> struct StringViewConverter<char, std::string> {
		static MutableStringView from(std::string& other) {
			// .data() returns a const pointer until C++17, so have to use &other[0]. It's guaranteed
			// to return a pointer to a single null character if the string is empty.
			return MutableStringView{&other[0], other.size(), StringViewFlags::NullTerminated};
		}
		static std::string to(MutableStringView other) {
			return std::string{other.data(), other.size()};
		}
	};

}}}