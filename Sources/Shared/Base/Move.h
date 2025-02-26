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

#include "../CommonBase.h"

#include <type_traits>

#if defined(DEATH_MSVC2015_COMPATIBILITY)
#	include <utility>	/* for std::swap() ambiguity workaround below */
#endif

namespace Death {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	// forward() and move() are basically copied from libstdc++'s bits/move.h

	/**
		@brief Forwards an l-value

		Returns @cpp static_cast<T&&>(t) @ce. Equivalent to @ref std::forward(), which is used to implement
		perfect forwarding, but without the @cpp #include <utility> @ce dependency and guaranteed to be
		@cpp constexpr @ce even in C++11.
	*/
	template<class T> constexpr T&& forward(typename std::remove_reference<T>::type& t) noexcept {
		return static_cast<T&&>(t);
	}

	/**
		@brief Forwards an r-value

		Returns @cpp static_cast<T&&>(t) @ce. Equivalent to @ref std::forward(), which is used to implement
		perfect forwarding, but without the @cpp #include <utility> @ce dependency and guaranteed to be
		@cpp constexpr @ce even in C++11.
	*/
	template<class T> constexpr T&& forward(typename std::remove_reference<T>::type&& t) noexcept {
		/* bits/move.h in libstdc++ has this and it makes sense to have it here
		   also, although I can't really explain what accidents it prevents */
		static_assert(!std::is_lvalue_reference<T>::value, "T cannot be a l-value reference");
		return static_cast<T&&>(t);
	}

	/**
		@brief Converts a value to an r-value

		Returns @cpp static_cast<typename std::remove_reference<T>::type&&>(t) @ce. Equivalent to
		@m_class{m-doc-external} [std::move()](https://en.cppreference.com/w/cpp/utility/move), but without
		the @cpp #include <utility> @ce dependency and guaranteed to be @cpp constexpr @ce even in C++11.
	*/
	template<class T> constexpr typename std::remove_reference<T>::type&& move(T&& t) noexcept {
		return static_cast<typename std::remove_reference<T>::type&&>(t);
	}

	/**
		@brief Swaps two values

		Swaps specified values. Equivalent to @ref std::swap(), but without the @cpp #include <utility> @ce
		dependency, and without the internals delegating to @m_class{m-doc-external} [std::move()](https://en.cppreference.com/w/cpp/utility/move),
		hurting debug performance. In order to keep supporting custom specializations, the usage pattern
		should be similar to the standard utility, i.e. with @cpp using Death::swap @ce.

		@partialsupport On @ref DEATH_MSVC2015_COMPATIBILITY "MSVC 2015" it's just an alias to @ref std::swap(),
			as compiler limitations prevent creating an alternative that wouldn't conflict.
	*/
	/* The common_type is to prevent ambiguity with (also unrestricted) std::swap.
	   Besides resolving ambiguity, in practice it means that std::swap() will get preferred over
	   this overload in all cases where ADL finds it due to a STL type being used. Which means
	   potentially slightly worse debug perf than if this overload was used for those too, due to
	   libstdc++, libc++ and MSVC STL all delegating to move() instead of inlining it. */
#if defined(DOXYGEN_GENERATING_OUTPUT)
	template<class T> void swap(T& a, T& b) noexcept(...);
#elif !defined(DEATH_MSVC2015_COMPATIBILITY)
	template<class T> void swap(T& a, typename std::common_type<T>::type& b) noexcept(std::is_nothrow_move_constructible<T>::value && std::is_nothrow_move_assignable<T>::value) {
		/* "Deinlining" move() for nicer debug performance */
		T tmp = static_cast<T&&>(a);
		a = static_cast<T&&>(b);
		b = static_cast<T&&>(tmp);
	}
#else
	/* On MSVC 2015 an ambiguous overload is caused even in cases where MSVC 2017+ and other compilers
	   don't have the problem. Since support for this compiler isn't really important anymore, the STL
	   variant is simply brought into this namespace. */
	using std::swap;
#endif

	/**
		@brief Swaps two arrays

		Does the same as @ref swap(T&, T&), but for every array element.
	*/
#if defined(DOXYGEN_GENERATING_OUTPUT)
	template<std::size_t size, class T> void swap(T(&a)[size], T(&b)[size]) noexcept(...);
#elif !defined(DEATH_MSVC2015_COMPATIBILITY)
	/* Using std::common_type<T>::type(&)[size] doesn't work on MSVC 2017 and 2019 (possibly also 2022 and `/permisive-`) */
	template<std::size_t size, class T> void swap(T(&a)[size], typename std::common_type<T(&)[size]>::type b) noexcept(std::is_nothrow_move_constructible<T>::value && std::is_nothrow_move_assignable<T>::value) {
		for(std::size_t i = 0; i != size; ++i) {
			/* "Deinlining" move() for nicer debug performance */
			T tmp = static_cast<T&&>(a[i]);
			a[i] = static_cast<T&&>(b[i]);
			b[i] = static_cast<T&&>(tmp);
		}
	}
#else
	/* using std::swap for MSVC 2015 already done above */
#endif

}