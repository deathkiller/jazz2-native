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

#include "../CommonBase.h"
#include "Tags.h"

#include <new>
#include <type_traits>

namespace Death::Containers
{
	/**
		@brief Compile-time-sized array
		@tparam size_   Array size
		@tparam T       Element type

		Like @ref Array, but with compile-time size information. Useful as a more featureful alternative to plain C arrays or @ref std::array,
		especially when it comes to initialization. A non-owning version of this container is a @ref StaticArrayView.
	 */
	template<std::size_t size_, class T> class StaticArray
	{
	public:
		enum : std::size_t {
			Size = size_
		};
		typedef T Type;

#if defined(DEATH_TARGET_LIBSTDCXX) && __GNUC__ < 5 && _GLIBCXX_RELEASE < 7
		template<class U = T, typename std::enable_if<std::integral_constant<bool, __has_trivial_constructor(U)>::value, int>::type = 0> explicit StaticArray(DefaultInitT) {}
		template<class U = T, typename std::enable_if<!std::integral_constant<bool, __has_trivial_constructor(U)>::value, int>::type = 0> explicit StaticArray(DefaultInitT)
#else
		template<class U = T, typename std::enable_if<std::is_trivially_constructible<U>::value, int>::type = 0> explicit StaticArray(DefaultInitT) {}
		template<class U = T, typename std::enable_if<!std::is_trivially_constructible<U>::value, int>::type = 0> explicit StaticArray(DefaultInitT)
#endif
#if !defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG) || __GNUC__*100 + __GNUC_MINOR__ >= 10003
			: _data() {}
#else
			{
				for(T& i: _data) new(&i) T();
			}
#endif

		explicit StaticArray(ValueInitT) : _data() {}

		explicit StaticArray(NoInitT) {}

		template<class ...Args> explicit StaticArray(DirectInitT, Args&&... args);

		template<class ...Args> explicit StaticArray(InPlaceInitT, Args&&... args) : _data{std::forward<Args>(args)...} {
			static_assert(sizeof...(args) == size_, "Containers::StaticArray: Wrong number of initializers");
		}

		explicit StaticArray() : StaticArray{ValueInit} {}

		template<class First, class ...Next, class = typename std::enable_if<std::is_convertible<First&&, T>::value>::type> /*implicit*/ StaticArray(First&& first, Next&&... next) : StaticArray{InPlaceInit, std::forward<First>(first), std::forward<Next>(next)...} {}

		StaticArray(const StaticArray<size_, T>& other) noexcept(std::is_nothrow_copy_constructible<T>::value);

		StaticArray(StaticArray<size_, T>&& other) noexcept(std::is_nothrow_move_constructible<T>::value);

		~StaticArray();

		StaticArray<size_, T>& operator=(const StaticArray<size_, T>&) noexcept(std::is_nothrow_copy_constructible<T>::value);

		StaticArray<size_, T>& operator=(StaticArray<size_, T>&&) noexcept(std::is_nothrow_move_constructible<T>::value);

		template<class U, class = decltype(Implementation::StaticArrayViewConverter<size_, T, U>::to(std::declval<StaticArrayView<size_, T>>()))> /*implicit*/ operator U() {
			return Implementation::StaticArrayViewConverter<size_, T, U>::to(*this);
		}

		template<class U, class = decltype(Implementation::StaticArrayViewConverter<size_, const T, U>::to(std::declval<StaticArrayView<size_, const T>>()))> constexpr /*implicit*/ operator U() const {
			return Implementation::StaticArrayViewConverter<size_, const T, U>::to(*this);
		}

		/*implicit*/ operator T* ()& {
			return _data;
		}

		/*implicit*/ operator const T* () const& {
			return _data;
		}

		T* data() {
			return _data;
		}
		const T* data() const {
			return _data;
		}

		constexpr std::size_t size() const {
			return size_;
		}

		constexpr bool empty() const {
			return !size_;
		}

		T* begin() {
			return _data;
		}
		const T* begin() const {
			return _data;
		}
		const T* cbegin() const {
			return _data;
		}

		T* end() {
			return _data + size_;
		}
		const T* end() const {
			return _data + size_;
		}
		const T* cend() const {
			return _data + size_;
		}

		T& front() {
			return _data[0];
		}
		const T& front() const {
			return _data[0];
		}

		T& back() {
			return _data[size_ - 1];
		}
		const T& back() const {
			return _data[size_ - 1];
		}

		ArrayView<T> slice(T* begin, T* end) {
			return ArrayView<T>(*this).slice(begin, end);
		}
		ArrayView<const T> slice(const T* begin, const T* end) const {
			return ArrayView<const T>(*this).slice(begin, end);
		}
		ArrayView<T> slice(std::size_t begin, std::size_t end) {
			return ArrayView<T>(*this).slice(begin, end);
		}
		ArrayView<const T> slice(std::size_t begin, std::size_t end) const {
			return ArrayView<const T>(*this).slice(begin, end);
		}

		template<std::size_t viewSize> StaticArrayView<viewSize, T> slice(T* begin) {
			return ArrayView<T>(*this).template slice<viewSize>(begin);
		}
		template<std::size_t viewSize> StaticArrayView<viewSize, const T> slice(const T* begin) const {
			return ArrayView<const T>(*this).template slice<viewSize>(begin);
		}
		template<std::size_t viewSize> StaticArrayView<viewSize, T> slice(std::size_t begin) {
			return ArrayView<T>(*this).template slice<viewSize>(begin);
		}
		template<std::size_t viewSize> StaticArrayView<viewSize, const T> slice(std::size_t begin) const {
			return ArrayView<const T>(*this).template slice<viewSize>(begin);
		}

		template<std::size_t begin_, std::size_t end_> StaticArrayView<end_ - begin_, T> slice() {
			return StaticArrayView<size_, T>(*this).template slice<begin_, end_>();
		}

		template<std::size_t begin_, std::size_t end_> StaticArrayView<end_ - begin_, const T> slice() const {
			return StaticArrayView<size_, const T>(*this).template slice<begin_, end_>();
		}

		ArrayView<T> prefix(T* end) {
			return ArrayView<T>(*this).prefix(end);
		}
		ArrayView<const T> prefix(const T* end) const {
			return ArrayView<const T>(*this).prefix(end);
		}
		ArrayView<T> prefix(std::size_t end) {
			return ArrayView<T>(*this).prefix(end);
		}
		ArrayView<const T> prefix(std::size_t end) const {
			return ArrayView<const T>(*this).prefix(end);
		}

		template<std::size_t viewSize> StaticArrayView<viewSize, T> prefix();
		template<std::size_t viewSize> StaticArrayView<viewSize, const T> prefix() const;

		ArrayView<T> suffix(T* begin) {
			return ArrayView<T>(*this).suffix(begin);
		}
		ArrayView<const T> suffix(const T* begin) const {
			return ArrayView<const T>(*this).suffix(begin);
		}
		ArrayView<T> suffix(std::size_t begin) {
			return ArrayView<T>(*this).suffix(begin);
		}
		ArrayView<const T> suffix(std::size_t begin) const {
			return ArrayView<const T>(*this).suffix(begin);
		}

		template<std::size_t begin_> StaticArrayView<size_ - begin_, T> suffix() {
			return StaticArrayView<size_, T>(*this).template suffix<begin_>();
		}

		template<std::size_t begin_> StaticArrayView<size_ - begin_, const T> suffix() const {
			return StaticArrayView<size_, const T>(*this).template suffix<begin_>();
		}

		ArrayView<T> except(std::size_t count) {
			return ArrayView<T>(*this).except(count);
		}
		ArrayView<const T> except(std::size_t count) const {
			return ArrayView<const T>(*this).except(count);
		}

		template<std::size_t count> StaticArrayView<size_ - count, T> except() {
			return StaticArrayView<size_, T>(*this).template except<count>();
		}
		template<std::size_t count> StaticArrayView<size_ - count, const T> except() const {
			return StaticArrayView<size_, const T>(*this).template except<count>();
		}

	private:
		union {
			T _data[size_];
		};
	};

	template<std::size_t size, class T> constexpr ArrayView<T> arrayView(StaticArray<size, T>& array) {
		return ArrayView<T>{array};
	}

	template<std::size_t size, class T> constexpr ArrayView<const T> arrayView(const StaticArray<size, T>& array) {
		return ArrayView<const T>{array};
	}

	template<std::size_t size, class T> constexpr StaticArrayView<size, T> staticArrayView(StaticArray<size, T>& array) {
		return StaticArrayView<size, T>{array};
	}

	template<std::size_t size, class T> constexpr StaticArrayView<size, const T> staticArrayView(const StaticArray<size, T>& array) {
		return StaticArrayView<size, const T>{array};
	}

	template<class U, std::size_t size, class T> StaticArrayView<size * sizeof(T) / sizeof(U), U> arrayCast(StaticArray<size, T>& array) {
		return arrayCast<U>(staticArrayView(array));
	}

	template<class U, std::size_t size, class T> StaticArrayView<size * sizeof(T) / sizeof(U), const U> arrayCast(const StaticArray<size, T>& array) {
		return arrayCast<const U>(staticArrayView(array));
	}

	template<std::size_t size_, class T> constexpr std::size_t arraySize(const StaticArray<size_, T>&) {
		return size_;
	}

	template<std::size_t size_, class T> template<class ...Args> StaticArray<size_, T>::StaticArray(DirectInitT, Args&&... args) : StaticArray { NoInit } {
		for (T& i : _data) {
			Implementation::construct(i, std::forward<Args>(args)...);
		}
	}

	template<std::size_t size_, class T> StaticArray<size_, T>::StaticArray(const StaticArray<size_, T>& other) noexcept(std::is_nothrow_copy_constructible<T>::value) : StaticArray { NoInit } {
		for (std::size_t i = 0; i != other.size(); ++i) {
			new(&_data[i]) T { other._data[i] };
		}
	}

	template<std::size_t size_, class T> StaticArray<size_, T>::StaticArray(StaticArray<size_, T>&& other) noexcept(std::is_nothrow_move_constructible<T>::value) : StaticArray { NoInit } {
		for (std::size_t i = 0; i != other.size(); ++i) {
			new(&_data[i]) T { std::move(other._data[i]) };
		}
	}

	template<std::size_t size_, class T> StaticArray<size_, T>::~StaticArray() {
		for (T& i : _data) {
			i.~T();
		}
	}

	template<std::size_t size_, class T> StaticArray<size_, T>& StaticArray<size_, T>::operator=(const StaticArray<size_, T>& other) noexcept(std::is_nothrow_copy_constructible<T>::value) {
		for (std::size_t i = 0; i != other.size(); ++i) {
			_data[i] = other._data[i];
		}
		return *this;
	}

	template<std::size_t size_, class T> StaticArray<size_, T>& StaticArray<size_, T>::operator=(StaticArray<size_, T>&& other) noexcept(std::is_nothrow_move_constructible<T>::value) {
		using std::swap;
		for (std::size_t i = 0; i != other.size(); ++i) {
			swap(_data[i], other._data[i]);
		}
		return *this;
	}

	template<std::size_t size_, class T> template<std::size_t viewSize> StaticArrayView<viewSize, T> StaticArray<size_, T>::prefix() {
		static_assert(viewSize <= size_, "Prefix size too large");
		return StaticArrayView<viewSize, T>{_data};
	}

	template<std::size_t size_, class T> template<std::size_t viewSize> StaticArrayView<viewSize, const T> StaticArray<size_, T>::prefix() const {
		static_assert(viewSize <= size_, "Prefix size too large");
		return StaticArrayView<viewSize, const T>{_data};
	}

	namespace Implementation
	{
		template<class U, std::size_t size, class T> struct ArrayViewConverter<U, StaticArray<size, T>> {
			template<class V = U> constexpr static typename std::enable_if<std::is_convertible<T*, V*>::value, ArrayView<U>>::type from(StaticArray<size, T>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return { &other[0], other.size() };
			}
		};
		template<class U, std::size_t size, class T> struct ArrayViewConverter<const U, StaticArray<size, T>> {
			template<class V = U> constexpr static typename std::enable_if<std::is_convertible<T*, V*>::value, ArrayView<const U>>::type from(const StaticArray<size, T>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return { &other[0], other.size() };
			}
		};
		template<class U, std::size_t size, class T> struct ArrayViewConverter<const U, StaticArray<size, const T>> {
			template<class V = U> constexpr static typename std::enable_if<std::is_convertible<T*, V*>::value, ArrayView<const U>>::type from(const StaticArray<size, const T>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return { &other[0], other.size() };
			}
		};
		template<std::size_t size, class T> struct ErasedArrayViewConverter<StaticArray<size, T>> : ArrayViewConverter<T, StaticArray<size, T>> {};
		template<std::size_t size, class T> struct ErasedArrayViewConverter<const StaticArray<size, T>> : ArrayViewConverter<const T, StaticArray<size, T>> {};

		template<class U, std::size_t size, class T> struct StaticArrayViewConverter<size, U, StaticArray<size, T>> {
			template<class V = U> constexpr static typename std::enable_if<std::is_convertible<T*, V*>::value, StaticArrayView<size, U>>::type from(StaticArray<size, T>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return StaticArrayView<size, T>{&other[0]};
			}
		};
		template<class U, std::size_t size, class T> struct StaticArrayViewConverter<size, const U, StaticArray<size, T>> {
			template<class V = U> constexpr static typename std::enable_if<std::is_convertible<T*, V*>::value, StaticArrayView<size, const U>>::type from(const StaticArray<size, T>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return StaticArrayView<size, const T>(&other[0]);
			}
		};
		template<class U, std::size_t size, class T> struct StaticArrayViewConverter<size, const U, StaticArray<size, const T>> {
			template<class V = U> constexpr static typename std::enable_if<std::is_convertible<T*, V*>::value, StaticArrayView<size, const U>>::type from(const StaticArray<size, const T>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return StaticArrayView<size, const T>(&other[0]);
			}
		};
		template<std::size_t size, class T> struct ErasedStaticArrayViewConverter<StaticArray<size, T>> : StaticArrayViewConverter<size, T, StaticArray<size, T>> {};
		template<std::size_t size, class T> struct ErasedStaticArrayViewConverter<const StaticArray<size, T>> : StaticArrayViewConverter<size, const T, StaticArray<size, T>> {};

	}
}