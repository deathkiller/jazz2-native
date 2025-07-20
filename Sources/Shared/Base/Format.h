// Copyright © 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016,
//             2017, 2018, 2019, 2020, 2021, 2022, 2023, 2024, 2025
//           Vladimír Vondruš <mosra@centrum.cz> and contributors
// Copyright © 2020-2025 Dan R.
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

#include "../Containers/Containers.h"
#include "../Containers/Tags.h"

namespace Death {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Formats a string

		Provides type-safe formatting of arbitrary types into a template string,
		similar in syntax to Python's [format()](https://docs.python.org/3.4/library/string.html#format-string-syntax).

		# Templating language

		Formatting placeholders are denoted by `{}`, which can have either implicit
		ordering (as shown above), or be numbered, such as `{2}`. Zero means first item
		from @p args, it's allowed to repeat the numbers. An implicit placeholder
		following a numbered one will get next position after.

		Unlike in Python, it's allowed to both have more placeholders than arguments or
		more arguments than placeholders. Extraneous placeholders are copied to the
		output verbatim, extraneous arguments are simply ignored.

		In order to write a literal curly brace to the output, simply double it.

		# Data type support

		@m_class{m-fullwidth}

		| Type                                  | Default behavior
		| ------------------------------------- | ------------------------------------
		| @cpp char @ce, @cpp signed char @ce, @cpp unsigned char @ce | Written as a base-10 integer (*not as a character*)
		| @cpp short @ce, @cpp unsigned short @ce | Written as a base-10 integer
		| @cpp int @ce, @cpp unsigned int @ce   | Written as a base-10 integer
		| @cpp long @ce, @cpp unsigned long @ce | Written as a base-10 integer
		| @cpp long long @ce, @cpp unsigned long long @ce | Written as a base-10 integer
		| @cpp float @ce <b></b>    | Written as a float with 6 significant digits by default
		| @cpp double @ce <b></b>   | Written as a float with 15 significant digits by default
		| @cpp long double @ce <b></b> | Written as a float, by default with 18 significant digits on platforms \n with 80-bit @cpp long double @ce and 15 digits on platforms @ref CORRADE_LONG_DOUBLE_SAME_AS_DOUBLE "where it is 64-bit"
		| @cpp bool @ce <b></b> | Written as @cpp true @ce / @cpp false @ce
		| @cpp char* @ce <b></b> | Written as a sequence of characters until @cpp '\0' @ce (which is not written)
		| @ref Containers::StringView, \n @ref Containers::MutableStringView "MutableStringView", @ref Containers::String "String" | Written as a sequence of @ref Containers::StringView::size() characters

		# Advanced formatting options

		Advanced formatting such as precision or presentation type is possible by
		putting extra options after a semicolon, following the optional placeholder
		number, such as `{:x}` to print an integer value in hexadecimal. In general,
		the syntax similar to the @ref std::printf() -style formatting, with the
		addition of `{}` and `:` used instead of `%` --- for example, @cpp "%.2f" @ce
		can be translated to @cpp "{:.2f}" @ce.

		The full placeholder syntax is the following, again a subset of the Python
		[format()](https://docs.python.org/3.4/library/string.html#format-string-syntax):

			{[number][:[.precision][type]]}

		The `type` is a single character specifying output conversion:

		Value           | Meaning
		--------------- | -------
		@cpp 'c' @ce <b></b> | Character. Valid only for 8-, 16- and 32-bit integer types. At the moment, arbitrary UTF-32 codepoints don't work, only 7-bit ASCII values have a guaranteed output.
		@cpp 'd' @ce <b></b> | Decimal integer (base 10). Valid only for integer types. Default for integers if nothing is specified.
		@cpp 'o' @ce <b></b> | Octal integer (base 8). Valid only for integer types.
		@cpp 'x' @ce <b></b> | Hexadecimal integer (base 16) with lowercase letters a--f. Valid only for integer types.
		@cpp 'X' @ce <b></b> | Hexadecimal integer with uppercase letters A--F. Valid only for integer types.
		@cpp 'g' @ce <b></b> | General floating-point, formatting the value either in exponent notation or fixed-point format depending on its magnitude. The exponent `e` and special values such as `nan` or `inf` are printed lowercase. Valid only for floating-point types.
		@cpp 'G' @ce <b></b> | General floating-point. The exponent `E` and special values such as `NAN` or `INF` are printed uppercase. Valid only for floating-point types.
		@cpp 'e' @ce <b></b> | Exponent notation. The exponent `e` and special values such as `nan` or `inf` are printed lowercase. Valid only for floating-point types.
		@cpp 'E' @ce <b></b> | Exponent notation. The exponent `E` and special values such as `NAN` or `INF` are printed uppercase. Valid only for floating-point types.
		@cpp 'f' @ce <b></b> | Fixed point. The exponent `e` and special values such as `nan` or `inf` are printed lowercase. Valid only for floating-point types.
		@cpp 'F' @ce <b></b> | Fixed point. The exponent `E` and special values such as `NAN` or `INF` are printed uppercase. Valid only for floating-point types.
		<em>none</em>   | Default based on type, equivalent to @cpp 'd' @ce for integral types and @cpp 'g' @ce for floating-point types. The only valid specifier for strings.

		The `precision` field specifies a precision of the output. It's interpreted
		differently based on the data type:

		Type            | Meaning
		--------------- | -------
		Integers, except for the @cpp 'c' @ce type specifier | If the number of decimals is smaller than `precision`, the integer gets padded with the `0` character from the left. If both the number and `precision` is @cpp 0 @ce, nothing is written to the output. Default `precision` is @cpp 1 @ce.
		Floating-point types with default or @cpp 'g' @ce / @cpp 'G' @ce type specifier | The number is printed with *at most* `precision` significant digits. Default `precision` depends on data type, see the type support table above.
		Floating-point types with @cpp 'e' @ce / @cpp 'E' @ce type specifier | The number is always printed with *exactly* one decimal, `precision` decimal points (including trailing zeros) and the exponent. Default `precision` depends on data type, see the type support table above.
		Floating-point types with @cpp 'f' @ce / @cpp 'F' @ce type specifier | The number is always printed with *exactly* `precision` decimal points including trailing zeros. Default `precision` depends on data type, see the type support table above.
		Strings, characters (integers with the @cpp 'c' @ce type specifier) | If the string length is larger than `precision`, only the first `precision` *bytes* are written to the output. Default `precision` is unlimited. Note that this doesn't work with UTF-8 at the moment and specifying `precision` of @cpp 0 @ce doesn't give the expected output for characters.

		# Performance

		This function always does exactly one allocation for the output array. See
		@ref formatInto(Containers::MutableStringView, const char*, const Args&... args)
		for a completely zero-allocation alternative.
	*/
#ifdef DOXYGEN_GENERATING_OUTPUT
	template<class ...Args> Containers::String format(const char* format, const Args&... args);
#else
	/* Done this way to avoid including <Containers/StringView.h> and <Containers/String.h> for the return type */
	template<class ...Args, class String = Containers::String> String format(const char* format, const Args&... args);
#endif

	/**
		@brief Formats a string into an existing buffer

		Writes formatted output to given @p buffer, expecting that it is large enough.
		The formatting is done completely without any allocation. Returns total amount
		of bytes written, *does not* write any terminating @cpp '\0' @ce character.

		See @ref format() for more information about usage and templating language.
	*/
#ifdef DOXYGEN_GENERATING_OUTPUT
	template<class ...Args> std::size_t formatInto(Containers::MutableStringView buffer, const char* format, const Args&... args);
#else
	/* Done this way to avoid including <Containers/StringView.h> */
	template<class ...Args, class MutableStringView = Containers::MutableStringView> std::size_t formatInto(MutableStringView buffer, const char* format, const Args&... args);
#endif

	/** @overload */
	template<class ...Args, std::size_t size> std::size_t formatInto(char(&buffer)[size], const char* format, const Args&... args);

	namespace Implementation
	{
		enum class FormatType : unsigned char;

		struct FormatContext;

		template<class T, class = void> struct Formatter;

		template<> struct Formatter<int> {
			static std::size_t format(const Containers::MutableStringView& buffer, int value, FormatContext& context);
		};
		template<> struct Formatter<short> : Formatter<int> {};

		template<> struct Formatter<unsigned int> {
			static std::size_t format(const Containers::MutableStringView& buffer, unsigned int value, FormatContext& context);
		};
		template<> struct Formatter<unsigned short> : Formatter<unsigned int> {};

		// The char is signed or unsigned depending on platform (ARM has it unsigned by default for example,
		// because it maps better to the instruction set), and thus std::int8_t is usually a typedef to `signed char`.
		// Be sure to support that as well, `signed short` and such OTOH isn't so important.
		template<> struct Formatter<char> : Formatter<int> {};
		template<> struct Formatter<signed char> : Formatter<int> {};
		template<> struct Formatter<unsigned char> : Formatter<unsigned int> {};

		template<> struct Formatter<long long> {
			static std::size_t format(const Containers::MutableStringView& buffer, long long value, FormatContext& context);
		};
		template<> struct Formatter<long> : Formatter<long long> {};

		template<> struct Formatter<unsigned long long> {
			static std::size_t format(const Containers::MutableStringView& buffer, unsigned long long value, FormatContext& context);
		};
		template<> struct Formatter<unsigned long> : Formatter<unsigned long long> {};

		template<> struct Formatter<float> {
			static std::size_t format(const Containers::MutableStringView& buffer, float value, FormatContext& context);
		};
		template<> struct Formatter<double> {
			static std::size_t format(const Containers::MutableStringView& buffer, double value, FormatContext& context);
		};
		template<> struct Formatter<long double> {
			static std::size_t format(const Containers::MutableStringView& buffer, long double value, FormatContext& context);
		};
		template<> struct Formatter<bool> {
			static std::size_t format(const Containers::MutableStringView& buffer, bool value, FormatContext& context);
		};
		template<> struct Formatter<const char*> {
			static std::size_t format(const Containers::MutableStringView& buffer, const char* value, FormatContext& context);
		};
		template<> struct Formatter<char*> : Formatter<const char*> {};
		template<> struct Formatter<Containers::StringView> {
			static std::size_t format(const Containers::MutableStringView& buffer, Containers::StringView value, FormatContext& context);
		};
		template<> struct Formatter<Containers::MutableStringView> : Formatter<Containers::StringView> {};
		template<> struct Formatter<Containers::String> : Formatter<Containers::StringView> {};

		// If the type is an enum, use its underlying type, assuming the enum is convertible to it
		template<class T> struct Formatter<T, typename std::enable_if<std::is_enum<T>::value>::type> {
			static std::size_t format(const Containers::MutableStringView& buffer, T value, FormatContext& context) {
				return Formatter<typename std::underlying_type<T>::type>::format(buffer, static_cast<typename std::underlying_type<T>::type>(value), context);
			}
		};

		struct BufferFormatter {
			/*implicit*/ constexpr BufferFormatter() : _fn{}, _value{} {}

			template<class T> explicit BufferFormatter(const T& value) : _value{&value} {
				_fn = [](const Containers::MutableStringView& buffer, const void* value, FormatContext& context) {
					return Formatter<typename std::decay<T>::type>::format(buffer, *static_cast<const T*>(value), context);
				};
			}

			std::size_t operator()(const Containers::MutableStringView& buffer, FormatContext& context) const {
				return _fn(buffer, _value, context);
			}

		private:
			std::size_t(*_fn)(const Containers::MutableStringView&, const void*, FormatContext&);
			const void* _value;
		};

		std::size_t formatFormatters(char* buffer, std::size_t bufferSize, const char* format, BufferFormatter* formatters, std::size_t formattersCount);

		template<class ...Args> std::size_t formatArgs(char* buffer, std::size_t bufferSize, const char* format, const Args&... args) {
			Implementation::BufferFormatter formatters[sizeof...(args) + 1] { Implementation::BufferFormatter{args}..., {} };
			return Implementation::formatFormatters(buffer, bufferSize, format, formatters, sizeof...(args));
		}
	}

#ifndef DOXYGEN_GENERATING_OUTPUT
	template<class ...Args, class String> String format(const char* format, const Args&... args) {
		const std::size_t size = Implementation::formatArgs(nullptr, 0, format, args...);
		String string{Containers::NoInit, size};
		Implementation::formatArgs(string.data(), size + 1, format, args...);
		return string;
	}

	template<class ...Args, class MutableStringView> std::size_t formatInto(MutableStringView buffer, const char* format, const Args&... args) {
		return Implementation::formatArgs(buffer.data(), buffer.size(), format, args...);
	}

	template<class ...Args, std::size_t size> std::size_t formatInto(char(&buffer)[size], const char* format, const Args&... args) {
		return Implementation::formatArgs(buffer, size, format, args...);
	}
#endif

}