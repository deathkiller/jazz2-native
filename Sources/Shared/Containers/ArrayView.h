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
#include "../Asserts.h"

#include <cstddef>
#include <initializer_list>
#include <type_traits>
#include <utility>

namespace Death::Containers
{
	template<class> class ArrayView;
	template<std::size_t, class> class StaticArrayView;

	namespace Implementation
	{
		template<class, class> struct ArrayViewConverter;
		template<class> struct ErasedArrayViewConverter;
	}

	/**
		@brief Array view

		A non-owning wrapper around a sized continuous range of data, similar to a dynamic @ref std::span from C++20.
		For a variant with compile-time size information see @ref StaticArrayView, for sparse and multi-dimensional views
		see @ref StridedArrayView, for efficient bit manipulation see @ref BasicBitArrayView "BitArrayView". An owning
		version of this container is an @ref Array.
	*/
	template<class T> class ArrayView
	{
	public:
		typedef T Type;

		constexpr /*implicit*/ ArrayView(std::nullptr_t = nullptr) noexcept : _data{}, _size {} {}

		constexpr /*implicit*/ ArrayView(T* data, std::size_t size) noexcept : _data(data), _size(size) {}

		template<class U, std::size_t size, class = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
		constexpr /*implicit*/ ArrayView(U(&data)[size]) noexcept : _data{data}, _size{size} {
			static_assert(sizeof(T) == sizeof(U), "Type sizes are not compatible");
		}

		template<class U, class = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
		constexpr /*implicit*/ ArrayView(ArrayView<U> view) noexcept : _data{view}, _size{view.size()} {
			static_assert(sizeof(T) == sizeof(U), "Type sizes are not compatible");
		}

		template<std::size_t size, class U, class = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
		constexpr /*implicit*/ ArrayView(StaticArrayView<size, U> view) noexcept : _data{view}, _size{size} {
			static_assert(sizeof(U) == sizeof(T), "Type sizes are not compatible");
		}

		template<class U, class = decltype(Implementation::ArrayViewConverter<T, typename std::decay<U&&>::type>::from(std::declval<U&&>()))> constexpr /*implicit*/ ArrayView(U&& other) noexcept : ArrayView{Implementation::ArrayViewConverter<T, typename std::decay<U&&>::type>::from(std::forward<U>(other))} {}

		template<class U, class = decltype(Implementation::ArrayViewConverter<T, U>::to(std::declval<ArrayView<T>>()))> constexpr /*implicit*/ operator U() const {
			return Implementation::ArrayViewConverter<T, U>::to(*this);
		}

#if !defined(DEATH_MSVC2019_COMPATIBILITY)
		constexpr explicit operator bool() const {
			return _data;
		}
#endif

		constexpr /*implicit*/ operator T* () const {
			return _data;
		}

		constexpr T* data() const {
			return _data;
		}

		constexpr std::size_t size() const {
			return _size;
		}

		constexpr bool empty() const {
			return !_size;
		}

		constexpr T* begin() const {
			return _data;
		}
		constexpr T* cbegin() const {
			return _data;
		}

		constexpr T* end() const {
			return _data + _size;
		}
		constexpr T* cend() const {
			return _data + _size;
		}

		T& front() const;

		T& back() const;

		constexpr ArrayView<T> slice(T* begin, T* end) const;

		constexpr ArrayView<T> slice(std::size_t begin, std::size_t end) const;

		template<std::size_t viewSize> constexpr StaticArrayView<viewSize, T> slice(T* begin) const;

		template<std::size_t viewSize> constexpr StaticArrayView<viewSize, T> slice(std::size_t begin) const;

		template<std::size_t begin_, std::size_t end_> constexpr StaticArrayView<end_ - begin_, T> slice() const;

		constexpr ArrayView<T> prefix(T* end) const {
			return end ? slice(_data, end) : nullptr;
		}

		constexpr ArrayView<T> prefix(std::size_t end) const {
			return slice(0, end);
		}

		template<std::size_t end_> constexpr StaticArrayView<end_, T> prefix() const {
			return slice<0, end_>();
		}

		constexpr ArrayView<T> suffix(T* begin) const {
			return _data && !begin ? nullptr : slice(begin, _data + _size);
		}

		constexpr ArrayView<T> suffix(std::size_t begin) const {
			return slice(begin, _size);
		}

		constexpr ArrayView<T> except(std::size_t count) const {
			return slice(0, _size - count);
		}

	private:
		T* _data;
		std::size_t _size;
	};

	/**
		@brief Void array view

		Specialization of @ref ArrayView which is convertible from a compile-time array, @ref Array, @ref ArrayView
		or @ref StaticArrayView of any non-constant type and also any non-constant type convertible to them. Size
		for a particular type is recalculated to a size in bytes. This specialization doesn't provide any accessors
		besides @ref data(), because it has no use for the @cpp void @ce type. Instead, use @ref arrayCast(ArrayView<void>)
		to first cast the array to a concrete type and then access the particular elements.
	*/
	template<> class ArrayView<void>
	{
	public:
		typedef void Type;

		constexpr /*implicit*/ ArrayView(std::nullptr_t = nullptr) noexcept : _data{}, _size{} {}

		constexpr /*implicit*/ ArrayView(void* data, std::size_t size) noexcept : _data(data), _size(size) {}

		template<class T> constexpr /*implicit*/ ArrayView(T* data, std::size_t size) noexcept : _data(data), _size(size * sizeof(T)) {}

		template<class T, std::size_t size
			, class = typename std::enable_if<!std::is_const<T>::value>::type
		> constexpr /*implicit*/ ArrayView(T(&data)[size]) noexcept : _data(data), _size(size * sizeof(T)) {}

		template<class T
			, class = typename std::enable_if<!std::is_const<T>::value>::type
		> constexpr /*implicit*/ ArrayView(ArrayView<T> array) noexcept : _data(array), _size(array.size() * sizeof(T)) {}

		template<std::size_t size, class T
			, class = typename std::enable_if<!std::is_const<T>::value>::type
		> constexpr /*implicit*/ ArrayView(const StaticArrayView<size, T>& array) noexcept : _data{array}, _size{size * sizeof(T)} {}

		template<class T, class = decltype(Implementation::ErasedArrayViewConverter<typename std::decay<T&&>::type>::from(std::declval<T&&>()))> constexpr /*implicit*/ ArrayView(T&& other) noexcept : ArrayView{Implementation::ErasedArrayViewConverter<typename std::decay<T&&>::type>::from(other)} {}

#if !defined(DEATH_MSVC2019_COMPATIBILITY)
		constexpr explicit operator bool() const {
			return _data;
		}
#endif

		constexpr /*implicit*/ operator void* () const {
			return _data;
		}

		constexpr void* data() const {
			return _data;
		}

		constexpr std::size_t size() const {
			return _size;
		}

		constexpr bool empty() const {
			return !_size;
		}

	private:
		void* _data;
		std::size_t _size;
	};

	/**
		@brief Constant void array view

		Specialization of @ref ArrayView which is convertible from a compile-time array, @ref Array, @ref ArrayView
		or @ref StaticArrayView of any type and also any type convertible to them. Size for a particular type is recalculated
		to a size in bytes. This specialization doesn't provide any accessors besides @ref data(), because it has no use
		for the @cpp void @ce type. Instead, use @ref arrayCast(ArrayView<const void>) to first cast the array to a concrete
		type and then access the particular elements.
	*/
	template<> class ArrayView<const void>
	{
	public:
		typedef const void Type;

		constexpr /*implicit*/ ArrayView(std::nullptr_t = nullptr) noexcept : _data{}, _size{} {}

		constexpr /*implicit*/ ArrayView(const void* data, std::size_t size) noexcept : _data(data), _size(size) {}

		template<class T> constexpr /*implicit*/ ArrayView(const T* data, std::size_t size) noexcept : _data(data), _size(size * sizeof(T)) {}

		template<class T, std::size_t size> constexpr /*implicit*/ ArrayView(T(&data)[size]) noexcept : _data(data), _size(size * sizeof(T)) {}

		constexpr /*implicit*/ ArrayView(ArrayView<void> array) noexcept : _data{array}, _size{array.size()} {}

		template<class T> constexpr /*implicit*/ ArrayView(ArrayView<T> array) noexcept : _data(array), _size(array.size() * sizeof(T)) {}

		template<std::size_t size, class T> constexpr /*implicit*/ ArrayView(const StaticArrayView<size, T>& array) noexcept : _data{array}, _size{size * sizeof(T)} {}

		template<class T, class = decltype(Implementation::ErasedArrayViewConverter<const T>::from(std::declval<const T&>()))> constexpr /*implicit*/ ArrayView(const T& other) noexcept : ArrayView{Implementation::ErasedArrayViewConverter<const T>::from(other)} {}

#if !defined(DEATH_MSVC2019_COMPATIBILITY)
		constexpr explicit operator bool() const {
			return _data;
		}
#endif

		constexpr /*implicit*/ operator const void* () const {
			return _data;
		}

		constexpr const void* data() const {
			return _data;
		}

		constexpr std::size_t size() const {
			return _size;
		}

		constexpr bool empty() const {
			return !_size;
		}

	private:
		const void* _data;
		std::size_t _size;
	};

	template<class T> constexpr ArrayView<T> arrayView(T* data, std::size_t size) {
		return ArrayView<T>{data, size};
	}

	template<std::size_t size, class T> constexpr ArrayView<T> arrayView(T(&data)[size]) {
		return ArrayView<T>{data};
	}

	template<class T> ArrayView<const T> arrayView(std::initializer_list<T> list) {
		return ArrayView<const T>{list.begin(), list.size()};
	}

	template<std::size_t size, class T> constexpr ArrayView<T> arrayView(StaticArrayView<size, T> view) {
		return ArrayView<T>{view};
	}

	template<class T> constexpr ArrayView<T> arrayView(ArrayView<T> view) {
		return view;
	}

	template<class T, class U = decltype(Implementation::ErasedArrayViewConverter<typename std::remove_reference<T&&>::type>::from(std::declval<T&&>()))> constexpr U arrayView(T&& other) {
		return Implementation::ErasedArrayViewConverter<typename std::remove_reference<T&&>::type>::from(std::forward<T>(other));
	}

	template<class U, class T> ArrayView<U> arrayCast(ArrayView<T> view) {
		static_assert(std::is_standard_layout<T>::value, "The source type is not standard layout");
		static_assert(std::is_standard_layout<U>::value, "The target type is not standard layout");
		const std::size_t size = view.size() * sizeof(T) / sizeof(U);
		DEATH_ASSERT(size * sizeof(U) == view.size() * sizeof(T), {},
			"Containers::arrayCast(): Can't reinterpret %zu %zu-byte items into a %zu-byte type", view.size(), sizeof(T), sizeof(U));
		return { reinterpret_cast<U*>(view.begin()), size };
	}

	template<class U> ArrayView<U> arrayCast(ArrayView<const void> view) {
		static_assert(std::is_standard_layout<U>::value, "The target type is not standard layout");
		const std::size_t size = view.size() / sizeof(U);
		DEATH_ASSERT(size * sizeof(U) == view.size(), {},
			"Containers::arrayCast(): Can't reinterpret %zu bytes into a %zu-byte type", view.size(), sizeof(U));
		return { reinterpret_cast<U*>(view.data()), size };
	}

	template<class U> ArrayView<U> arrayCast(ArrayView<void> view) {
		auto out = arrayCast<const U>(ArrayView<const void>{view});
		return ArrayView<U>{const_cast<U*>(out.data()), out.size()};
	}

	template<class T> constexpr std::size_t arraySize(ArrayView<T> view) {
		return view.size();
	}

	template<std::size_t size_, class T> constexpr std::size_t arraySize(StaticArrayView<size_, T>) {
		return size_;
	}

	template<std::size_t size_, class T> constexpr std::size_t arraySize(T(&)[size_]) {
		return size_;
	}

	namespace Implementation
	{
		template<std::size_t, class, class> struct StaticArrayViewConverter;
		template<class> struct ErasedStaticArrayViewConverter;
	}

	/**
		@brief Compile-time-sized array view

		Like @ref ArrayView, but with compile-time size information. Similar to a fixed-size @ref std::span from C++2a.
		Implicitly convertible to an @ref ArrayView, explicitly convertible from it using the slicing APIs. An owning
		version of this container is a @ref StaticArray.
	*/
	template<std::size_t size_, class T> class StaticArrayView
	{
	public:
		typedef T Type;

		enum : std::size_t {
			Size = size_
		};

		constexpr /*implicit*/ StaticArrayView(std::nullptr_t = nullptr) noexcept : _data{} {}

		template<class U, class = typename std::enable_if<std::is_pointer<U>::value && !std::is_same<U, T(&)[size_]>::value>::type> constexpr explicit StaticArrayView(U data)
			noexcept : _data(data) {}

		template<class U, class = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
		constexpr /*implicit*/ StaticArrayView(U(&data)[size_]) noexcept : _data{data} {}

		template<class U, class = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
		constexpr /*implicit*/ StaticArrayView(StaticArrayView<size_, U> view) noexcept : _data{view} {
			static_assert(sizeof(T) == sizeof(U), "Type sizes are not compatible");
		}

		template<class U, class = decltype(Implementation::StaticArrayViewConverter<size_, T, typename std::decay<U&&>::type>::from(std::declval<U&&>()))> constexpr /*implicit*/ StaticArrayView(U&& other) noexcept : StaticArrayView{Implementation::StaticArrayViewConverter<size_, T, typename std::decay<U&&>::type>::from(std::forward<U>(other))} {}

		template<class U, class = decltype(Implementation::StaticArrayViewConverter<size_, T, U>::to(std::declval<StaticArrayView<size_, T>>()))> constexpr /*implicit*/ operator U() const {
			return Implementation::StaticArrayViewConverter<size_, T, U>::to(*this);
		}

#if !defined(DEATH_MSVC2019_COMPATIBILITY)
		constexpr explicit operator bool() const {
			return _data;
		}
#endif

		constexpr /*implicit*/ operator T* () const {
			return _data;
		}

		constexpr T* data() const {
			return _data;
		}

		constexpr std::size_t size() const {
			return size_;
		}

		constexpr bool empty() const {
			return !size_;
		}

		constexpr T* begin() const {
			return _data;
		}
		constexpr T* cbegin() const {
			return _data;
		}

		constexpr T* end() const {
			return _data + size_;
		}
		constexpr T* cend() const {
			return _data + size_;
		}

		T& front() const;

		T& back() const;

		constexpr ArrayView<T> slice(T* begin, T* end) const {
			return ArrayView<T>(*this).slice(begin, end);
		}
		constexpr ArrayView<T> slice(std::size_t begin, std::size_t end) const {
			return ArrayView<T>(*this).slice(begin, end);
		}

		template<std::size_t viewSize> constexpr StaticArrayView<viewSize, T> slice(T* begin) const {
			return ArrayView<T>(*this).template slice<viewSize>(begin);
		}
		template<std::size_t viewSize> constexpr StaticArrayView<viewSize, T> slice(std::size_t begin) const {
			return ArrayView<T>(*this).template slice<viewSize>(begin);
		}

		template<std::size_t begin_, std::size_t end_> constexpr StaticArrayView<end_ - begin_, T> slice() const;

		constexpr ArrayView<T> prefix(T* end) const {
			return ArrayView<T>(*this).prefix(end);
		}
		constexpr ArrayView<T> prefix(std::size_t end) const {
			return ArrayView<T>(*this).prefix(end);
		}

		template<std::size_t end_> constexpr StaticArrayView<end_, T> prefix() const {
			return slice<0, end_>();
		}

		constexpr ArrayView<T> suffix(T* begin) const {
			return ArrayView<T>(*this).suffix(begin);
		}
		constexpr ArrayView<T> suffix(std::size_t begin) const {
			return ArrayView<T>(*this).suffix(begin);
		}

		template<std::size_t begin_> constexpr StaticArrayView<size_ - begin_, T> suffix() const {
			return slice<begin_, size_>();
		}

		constexpr ArrayView<T> except(std::size_t count) const {
			return ArrayView<T>(*this).except(count);
		}

		template<std::size_t count> constexpr StaticArrayView<size_ - count, T> except() const {
			return slice<0, size_ - count>();
		}

	private:
		T* _data;
	};

	template<std::size_t size, class T> constexpr StaticArrayView<size, T> staticArrayView(T* data) {
		return StaticArrayView<size, T>{data};
	}

	template<std::size_t size, class T> constexpr StaticArrayView<size, T> staticArrayView(T(&data)[size]) {
		return StaticArrayView<size, T>{data};
	}

	template<std::size_t size, class T> constexpr StaticArrayView<size, T> staticArrayView(StaticArrayView<size, T> view) {
		return view;
	}

	template<class T, class U = decltype(Implementation::ErasedStaticArrayViewConverter<typename std::remove_reference<T&&>::type>::from(std::declval<T&&>()))> constexpr U staticArrayView(T&& other) {
		return Implementation::ErasedStaticArrayViewConverter<typename std::remove_reference<T&&>::type>::from(std::forward<T>(other));
	}

	template<class U, std::size_t size, class T> StaticArrayView<size * sizeof(T) / sizeof(U), U> arrayCast(StaticArrayView<size, T> view) {
		static_assert(std::is_standard_layout<T>::value, "The source type is not standard layout");
		static_assert(std::is_standard_layout<U>::value, "The target type is not standard layout");
		constexpr const std::size_t newSize = size * sizeof(T) / sizeof(U);
		static_assert(newSize * sizeof(U) == size * sizeof(T), "Type sizes are not compatible");
		return StaticArrayView<newSize, U>{reinterpret_cast<U*>(view.begin())};
	}

	template<class U, std::size_t size, class T> StaticArrayView<size * sizeof(T) / sizeof(U), U> arrayCast(T(&data)[size]) {
		return arrayCast<U>(StaticArrayView<size, T>{data});
	}

	template<class T> T& ArrayView<T>::front() const {
		DEATH_ASSERT(_size != 0, _data[0], "Containers::ArrayView::front(): View is empty");
		return _data[0];
	}

	template<class T> T& ArrayView<T>::back() const {
		DEATH_ASSERT(_size != 0, _data[_size - 1], "Containers::ArrayView::back(): View is empty");
		return _data[_size - 1];
	}

	template<class T> constexpr ArrayView<T> ArrayView<T>::slice(T* begin, T* end) const {
		return DEATH_CONSTEXPR_ASSERT(_data <= begin && begin <= end && end <= _data + _size,
				"Containers::ArrayView::slice(): Slice is out of range"),
			ArrayView<T>{begin, std::size_t(end - begin)};
	}

	template<class T> constexpr ArrayView<T> ArrayView<T>::slice(std::size_t begin, std::size_t end) const {
		return DEATH_CONSTEXPR_ASSERT(begin <= end && end <= _size,
				"Containers::ArrayView::slice(): Slice is out of range"),
			ArrayView<T>{_data + begin, end - begin};
	}

	template<std::size_t size_, class T> T& StaticArrayView<size_, T>::front() const {
		static_assert(size_ != 0, "View is empty");
		return _data[0];
	}

	template<std::size_t size_, class T> T& StaticArrayView<size_, T>::back() const {
		static_assert(size_ != 0, "View is empty");
		return _data[size_ - 1];
	}

	template<class T> template<std::size_t viewSize> constexpr StaticArrayView<viewSize, T> ArrayView<T>::slice(T* begin) const {
		return DEATH_CONSTEXPR_ASSERT(_data <= begin && begin + viewSize <= _data + _size,
				"Containers::ArrayView::slice(): Slice is out of range"),
			StaticArrayView<viewSize, T>{begin};
	}

	template<class T> template<std::size_t viewSize> constexpr StaticArrayView<viewSize, T> ArrayView<T>::slice(std::size_t begin) const {
		return DEATH_CONSTEXPR_ASSERT(begin + viewSize <= _size,
				"Containers::ArrayView::slice(): Slice is out of range"),
			StaticArrayView<viewSize, T>{_data + begin};
	}

	template<class T> template<std::size_t begin_, std::size_t end_> constexpr StaticArrayView<end_ - begin_, T> ArrayView<T>::slice() const {
		static_assert(begin_ < end_, "Fixed-size slice needs to have a positive size");
		return DEATH_CONSTEXPR_ASSERT(end_ <= _size,
				"Containers::ArrayView::slice(): Slice is out of range"),
			StaticArrayView<end_ - begin_, T>{_data + begin_};
	}

	template<std::size_t size_, class T> template<std::size_t begin_, std::size_t end_> constexpr StaticArrayView<end_ - begin_, T> StaticArrayView<size_, T>::slice() const {
		static_assert(begin_ < end_, "Fixed-size slice needs to have a positive size");
		static_assert(end_ <= size_, "Slice out of range");
		return StaticArrayView<end_ - begin_, T>{_data + begin_};
	}
}