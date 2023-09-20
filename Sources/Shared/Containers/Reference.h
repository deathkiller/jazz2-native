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

#include <type_traits>

namespace Death::Containers
{
	namespace Implementation
	{
		template<class, class> struct ReferenceConverter;
	}

	/**
		@brief Lightweight non-owning reference wrapper

		Equivalent to @ref std::reference_wrapper from C++11, provides a copyable non-owning wrapper over l-value references
		to allow storing them in containers. Unlike @ref std::reference_wrapper, this class does not provide @cpp operator() @ce
		and there are no equivalents to @ref std::ref() / @ref std::cref() as they are not deemed necessary --- in most contexts
		where @ref Reference is used, passing a plain reference works just as well. This class is trivially copyable
		(@ref std::reference_wrapper is guaranteed to be so only since C++17) and also works on incomplete types,
		which @ref std::reference_wrapper knows since C++20.

		This class is exclusively for l-value references.
	*/
	template<class T> class Reference
	{
	public:
		constexpr /*implicit*/ Reference(T& reference) noexcept : _reference{&reference} {}

		/**
		 * @brief Construct a reference from external representation
		 */
		template<class U, class = decltype(Implementation::ReferenceConverter<T, U>::from(std::declval<U>()))> constexpr /*implicit*/ Reference(U other) noexcept : Reference{Implementation::ReferenceConverter<T, U>::from(other)} { }

		/**
		 * @brief Construction from r-value references is not allowed
		 *
		 * A @ref MoveReference can be created from r-value references instead.
		 */
		Reference(T&&) = delete;

		/**
		 * @brief Construct a reference from another of a derived type
		 *
		 * Expects that @p T is a base of @p U.
		 */
		template<class U, class = typename std::enable_if<std::is_base_of<T, U>::value>::type> constexpr /*implicit*/ Reference(Reference<U> other) noexcept : _reference{other._reference} { }

		/**
		 * @brief Convert the reference to external representation
		 */
		template<class U, class = decltype(Implementation::ReferenceConverter<T, U>::to(std::declval<Reference<T>>()))> constexpr /*implicit*/ operator U() const
		{
			return Implementation::ReferenceConverter<T, U>::to(*this);
		}

		/** @brief Underlying reference */
		constexpr /*implicit*/ operator T& () const
		{
			return *_reference;
		}
		/** @overload */
		constexpr /*implicit*/ operator Reference<const T>() const
		{
			return *_reference;
		}

		/** @brief Underlying reference */
		constexpr T& get() const
		{
			return *_reference;
		}

		/**
		 * @brief Access the underlying reference
		 *
		 * @ref get(), @ref operator*()
		 */
		constexpr T* operator->() const
		{
			return _reference;
		}

		/**
		 * @brief Access the underlying reference
		 */
		constexpr T& operator*() const
		{
			return *_reference;
		}

	private:
		// For the conversion constructor
		template<class U> friend class Reference;

		T* _reference;
	};
}
