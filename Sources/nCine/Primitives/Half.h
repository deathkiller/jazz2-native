// Copyright © 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016,
//             2017, 2018, 2019, 2020, 2021, 2022, 2023, 2024
//           Vladimír Vondruš <mosra@centrum.cz> and contributors
// Copyright © 2020-2024 Dan R.
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

#include <Common.h>
#include <Containers/Tags.h>

namespace nCine
{
	using Death::Containers::NoInitT;

	/**
		@brief Half-precision float literal

		Represents a floating-point value in the [`binary16` format](https://en.wikipedia.org/wiki/Half-precision_floating-point_format).

		The sole purpose of this type is to make creation, conversion and visualization
		of half-float values easier. By design it doesn't support any arithmetic
		operations as not all CPU architecture have native support for half-floats and
		thus the operations would be done faster in a regular single-precision @cp float @ce.

		The class provides explicit conversion from and to @cp float @ce,
		equality comparison with correct treatment of NaN values, promotion and
		negation operator, a @link Literals::operator""_h() @endlink literal.
	*/
	class Half
	{
	public:
		/** @brief Default constructor */
		constexpr /*implicit*/ Half() noexcept : _data{} {}


		/** @brief Construct a half value from underlying 16-bit representation */
		constexpr explicit Half(std::uint16_t data) noexcept : _data{data} {}

		/** @brief Construct a half value from a 32-bit float representation */
		explicit Half(float value) noexcept : _data{PackHalf(value)} {}

		/**
		 * @brief Construct a half value from a 64-bit float representation
		 *
		 * Present only to aid generic code, so e.g. @cpp T(1.0) @ce works
		 * without being ambigous.
		 */
		explicit Half(double value) noexcept : _data{PackHalf(float(value))} {}

		/** @brief Construct without initializing the contents */
		explicit Half(NoInitT) noexcept {}

		/**
		 * @brief Equality comparison
		 *
		 * Returns `false` if one of the values is half-float representation of
		 * NaN, otherwise does bitwise comparison. Note that, unlike with other
		 * floating-point math types, due to the limited precision of half
		 * floats it's *not* a fuzzy compare.
		 */
		constexpr bool operator==(Half other) const {
			return (((_data & 0x7c00) == 0x7c00 && (_data & 0x03ff)) ||
					((other._data & 0x7c00) == 0x7c00 && (other._data & 0x03ff))) ?
				false : (_data == other._data);
		}

		/**
		 * @brief Non-equality comparison
		 *
		 * Simply negates the result of @ref operator==().
		 */
		constexpr bool operator!=(Half other) const {
			return !operator==(other);
		}

		/**
		 * @brief Promotion
		 *
		 * Returns the value as-is.
		 */
		constexpr Half operator+() const {
			return *this;
		}

		/** @brief Negation */
		constexpr Half operator-() const {
			return Half{std::uint16_t(_data ^ (1 << 15))};
		}

		/**
		 * @brief Conversion to underlying representation
		 *
		 * @see @ref data()
		 */
		constexpr explicit operator std::uint16_t() const { return _data; }

		/**
		 * @brief Conversion to 32-bit float representation
		 */
		explicit operator float() const { return UnpackHalf(_data); }

		/**
		 * @brief Underlying representation
		 *
		 * @see @ref operator std::uint16_t()
		 */
		constexpr std::uint16_t data() const { return _data; }

	private:
		std::uint16_t _data;

		/**
			@brief Pack 32-bit float value into 16-bit half-float representation

			See [Wikipedia](https://en.wikipedia.org/wiki/Half-precision_floating-point_format)
			for more information about half floats. NaNs are converted to NaNs and
			infinities to infinities, though their exact bit pattern is not preserved. Note
			that rounding mode is unspecified in order to save some cycles.

			Implementation based on CC0 / public domain code by *Fabian Giesen*,
			https://fgiesen.wordpress.com/2012/03/28/half-to-float-done-quic/ .
		*/
		static std::uint16_t PackHalf(float value);

		/**
			@brief Unpack 16-bit half-float value into 32-bit float representation

			See [Wikipedia](https://en.wikipedia.org/wiki/Half-precision_floating-point_format)
			for more information about half floats. NaNs are converted to NaNs and
			infinities to infinities, though their exact bit pattern is not preserved.

			Implementation based on CC0 / public domain code by *Fabian Giesen*,
			https://fgiesen.wordpress.com/2012/03/28/half-to-float-done-quic/ .
		*/
		static float UnpackHalf(std::uint16_t value);
	};

	namespace Literals
	{
		// According to https://wg21.link/CWG2521, space between "" and literal name is deprecated because _Uppercase or 
		// _double names could be treated as reserved depending on whether the space was present or not, and whitespace is
		// not load-bearing in any other contexts. Clang 17+ adds an off-by-default warning for this; GCC 4.8 however *requires*
		// the space there, so until GCC 4.8 support is dropped, we suppress this warning instead of removing the space.
		#if defined(DEATH_TARGET_CLANG) && __clang_major__ >= 17
		#	pragma clang diagnostic push
		#	pragma clang diagnostic ignored "-Wdeprecated-literal-operator"
		#endif

		/** @relatesalso nCine::Half
			@brief Half-float literal

			See @ref Half for more information.
		*/
		inline Half operator"" _h(long double value) {
			return Half(float(value));
		}

		#if defined(DEATH_TARGET_CLANG) && __clang_major__ >= 17
		#	pragma clang diagnostic pop
		#endif
	}
}
