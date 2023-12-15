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

namespace Death { namespace Containers {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	// Forward declarations for the Death::Containers namespace
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
		/** @brief Element type */
		typedef T Type;

		/**
		 * @brief Default constructor
		 *
		 * Creates an empty @cpp nullptr @ce view. Copy a non-empty @ref Array
		 * or @ref ArrayView onto the instance to make it useful.
		 */
		template<class U, class = typename std::enable_if<std::is_same<std::nullptr_t, U>::value>::type> constexpr /*implicit*/ ArrayView(U) noexcept : _data{}, _size{} {}

		constexpr /*implicit*/ ArrayView() noexcept : _data{}, _size{} {}

		/**
		 * @brief Construct a view on an array with explicit size
		 * @param data      Data pointer
		 * @param size      Data size
		 *
		 * @see @ref arrayView(T*, std::size_t)
		 */
		constexpr /*implicit*/ ArrayView(T* data, std::size_t size) noexcept : _data(data), _size(size) {}

		/**
		* @brief Construct a view on a fixed-size array
		* @param data      Fixed-size array
		*
		* Enabled only if @cpp T* @ce is implicitly convertible to @cpp U* @ce.
		* Expects that both types have the same size.
		*/
		template<class U, std::size_t size, class = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
		constexpr /*implicit*/ ArrayView(U(&data)[size]) noexcept : _data{data}, _size{size} {
			static_assert(sizeof(T) == sizeof(U), "Type sizes are not compatible");
		}

		/**
		* @brief Construct a view on an @ref ArrayView
		*
		* Enabled only if @cpp T* @ce is implicitly convertible to @cpp U* @ce.
		* Expects that both types have the same size.
		*/
		template<class U, class = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
		constexpr /*implicit*/ ArrayView(ArrayView<U> view) noexcept : _data{view}, _size{view.size()} {
			static_assert(sizeof(T) == sizeof(U), "Type sizes are not compatible");
		}

		/**
		* @brief Construct a view on a @ref StaticArrayView
		*
		* Enabled only if @cpp T* @ce is implicitly convertible to @cpp U* @ce.
		* Expects that both types have the same size.
		*/
		template<std::size_t size, class U, class = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
		constexpr /*implicit*/ ArrayView(StaticArrayView<size, U> view) noexcept : _data{view}, _size{size} {
			static_assert(sizeof(U) == sizeof(T), "Type sizes are not compatible");
		}

		/** @brief Construct a view on an external type / from an external representation */
		template<class U, class = decltype(Implementation::ArrayViewConverter<T, typename std::decay<U&&>::type>::from(std::declval<U&&>()))> constexpr /*implicit*/ ArrayView(U&& other) noexcept : ArrayView{Implementation::ArrayViewConverter<T, typename std::decay<U&&>::type>::from(std::forward<U>(other))} {}
		
		/** @brief Convert the view to external representation */
		template<class U, class = decltype(Implementation::ArrayViewConverter<T, U>::to(std::declval<ArrayView<T>>()))> constexpr /*implicit*/ operator U() const {
			return Implementation::ArrayViewConverter<T, U>::to(*this);
		}

#if !defined(DEATH_MSVC2019_COMPATIBILITY)
		/** @brief Whether the view is non-empty */
		constexpr explicit operator bool() const { return _data; }
#endif

		/** @brief Conversion to the underlying type */
		constexpr /*implicit*/ operator T*() const { return _data; }

		/** @brief View data */
		constexpr T* data() const { return _data; }

		/** @brief View size */
		constexpr std::size_t size() const { return _size; }

		/** @brief Whether the view is empty */
		constexpr bool empty() const { return !_size; }

		/** @brief Pointer to the first element */
		constexpr T* begin() const { return _data; }
		/** @overload */
		constexpr T* cbegin() const { return _data; } 

		/** @brief Pointer to (one item after) the last element */
		constexpr T* end() const { return _data + _size; }
		/** @overload */
		constexpr T* cend() const { return _data + _size; }

		/**
		 * @brief First element
		 *
		 * Expects there is at least one element.
		 */
		constexpr T& front() const;

		/**
		 * @brief Last element
		 *
		 * Expects there is at least one element.
		 */
		constexpr T& back() const;

		/**
		 * @brief Element access
		 *
		 * Expects that @p i is less than @ref size().
		 */
		template<class U, class = typename std::enable_if<std::is_convertible<U, std::size_t>::value>::type> constexpr T& operator[](U i) const;

		/**
		 * @brief View slice
		 *
		 * Both arguments are expected to be in range.
		 */
		constexpr ArrayView<T> slice(T* begin, T* end) const;
		/** @overload */
		constexpr ArrayView<T> slice(std::size_t begin, std::size_t end) const;

		/**
		 * @brief View slice of given size
		 *
		 * Equivalent to @cpp data.slice(begin, begin + size) @ce.
		 */
		template<class U, class = typename std::enable_if<std::is_convertible<U, T*>::value && !std::is_convertible<U, std::size_t>::value>::type> constexpr ArrayView<T> sliceSize(U begin, std::size_t size) const {
			return slice(begin, begin + size);
		}
		/** @overload */
		constexpr ArrayView<T> sliceSize(std::size_t begin, std::size_t size) const {
			return slice(begin, begin + size);
		}

		/**
		 * @brief Fixed-size view slice
		 *
		 * Both @p begin and @cpp begin + size_ @ce are expected to be in
		 * range.
		 */
		template<std::size_t size_, class U, class = typename std::enable_if<std::is_convertible<U, T*>::value && !std::is_convertible<U, std::size_t>::value>::type> constexpr StaticArrayView<size_, T> slice(U begin) const;
		/** @overload */
		template<std::size_t viewSize> constexpr StaticArrayView<viewSize, T> slice(std::size_t begin) const;

		/**
		 * @brief Fixed-size view slice
		 *
		 * At compile time expects that @cpp begin < end_ @ce, at runtime that
		 * @p end_ is not larger than @ref size().
		 */
		template<std::size_t begin_, std::size_t end_> constexpr StaticArrayView<end_ - begin_, T> slice() const;

		/**
		* @brief Fixed-size view slice of given size
		*
		* Expects that `begin_ + size_` is not larger than @ref size().
		*/
		template<std::size_t begin_, std::size_t size_> constexpr StaticArrayView<size_, T> sliceSize() const {
			return slice<begin_, begin_ + size_>();
		}

		/**
		 * @brief View prefix until a pointer
		 *
		 * Equivalent to @cpp data.slice(data.begin(), end) @ce. If @p end is
		 * @cpp nullptr @ce, returns zero-sized @cpp nullptr @ce array.
		 */
		template<class U, class = typename std::enable_if<std::is_convertible<U, T*>::value && !std::is_convertible<U, std::size_t>::value>::type> constexpr ArrayView<T> prefix(U end) const {
			return static_cast<T*>(end) ? slice(_data, end) : nullptr;
		}

		/**
		 * @brief View suffix after a pointer
		 *
		 * Equivalent to @cpp data.slice(begin, data.end()) @ce. If @p begin is
		 * @cpp nullptr @ce and the original array isn't, returns zero-sized
		 * @cpp nullptr @ce array.
		 */
		constexpr ArrayView<T> suffix(T* begin) const {
			return _data && !begin ? nullptr : slice(begin, _data + _size);
		}

		/**
		 * @brief View on the first @p size items
		 *
		 * Equivalent to @cpp data.slice(0, size) @ce.
		 */
		constexpr ArrayView<T> prefix(std::size_t size) const {
			return slice(0, size);
		}

		/**
		 * @brief Fixed-size view on the first @p size_ items
		 *
		 * Equivalent to @cpp data.slice<0, size_>() @ce.
		 */
		template<std::size_t size_> constexpr StaticArrayView<size_, T> prefix() const {
			return slice<0, size_>();
		}

		/**
		 * @brief Fixed-size view on the last @p size_ items
		 *
		 * Equivalent to @cpp data.slice<size_>(data.size() - size_) @ce.
		 */
		template<std::size_t size_> constexpr StaticArrayView<size_, T> suffix() const {
			return slice<size_>(_size - size_);
		}

		/**
		 * @brief View except the first @p size items
		 *
		 * Equivalent to @cpp data.slice(size, data.size()) @ce.
		 */
		constexpr ArrayView<T> exceptPrefix(std::size_t size) const {
			return slice(size, _size);
		}

		/**
		 * @brief View except the last @p size items
		 *
		 * Equivalent to @cpp data.slice(0, data.size() - size) @ce.
		 */
		constexpr ArrayView<T> exceptSuffix(std::size_t size) const {
			return slice(0, _size - size);
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
		/** @brief Element type */
		typedef void Type;

		/**
		 * @brief Default constructor
		 *
		 * Creates an empty @cpp nullptr @ce view. Copy a non-empty @ref Array
		 * or @ref ArrayView onto the instance to make it useful.
		 */
		template<class U, class = typename std::enable_if<std::is_same<std::nullptr_t, U>::value>::type> constexpr /*implicit*/ ArrayView(U) noexcept : _data{}, _size{} {}

		constexpr /*implicit*/ ArrayView() noexcept : _data{}, _size{} {}

		/**
		 * @brief Construct a view on a type-erased array with explicit length
		 * @param data      Data pointer
		 * @param size      Data size in bytes
		 */
		constexpr /*implicit*/ ArrayView(void* data, std::size_t size) noexcept : _data(data), _size(size) {}

		/**
		 * @brief Construct a view on a typed array with explicit length
		 * @param data      Data pointer
		 * @param size      Data size
		 *
		 * Size is recalculated to size in bytes.
		 */
		template<class T> constexpr /*implicit*/ ArrayView(T* data, std::size_t size) noexcept : _data(data), _size(size * sizeof(T)) {}

		/**
		 * @brief Construct a view on a fixed-size array
		 * @param data      Fixed-size array
		 *
		 * Size in bytes is calculated automatically.
		 */
		template<class T, std::size_t size
			, class = typename std::enable_if<!std::is_const<T>::value>::type
		> constexpr /*implicit*/ ArrayView(T(&data)[size]) noexcept : _data(data), _size(size * sizeof(T)) {}

		/** @brief Construct a void view on any @ref ArrayView */
		template<class T
			, class = typename std::enable_if<!std::is_const<T>::value>::type
		> constexpr /*implicit*/ ArrayView(ArrayView<T> array) noexcept : _data(array), _size(array.size() * sizeof(T)) {}

		/** @brief Construct a void view on any @ref StaticArrayView */
		template<std::size_t size, class T
			, class = typename std::enable_if<!std::is_const<T>::value>::type
		> constexpr /*implicit*/ ArrayView(const StaticArrayView<size, T>& array) noexcept : _data{array}, _size{size * sizeof(T)} {}

		/** @brief Construct a view on an external type / from an external representation */
		template<class T, class = decltype(Implementation::ErasedArrayViewConverter<typename std::decay<T&&>::type>::from(std::declval<T&&>()))> constexpr /*implicit*/ ArrayView(T&& other) noexcept : ArrayView{Implementation::ErasedArrayViewConverter<typename std::decay<T&&>::type>::from(other)} {}

#if !defined(DEATH_MSVC2019_COMPATIBILITY)
		/** @brief Whether the view is non-empty */
		constexpr explicit operator bool() const { return _data; }
#endif

		/** @brief Conversion to the underlying type */
		constexpr /*implicit*/ operator void*() const { return _data; }

		/** @brief View data */
		constexpr void* data() const { return _data; }

		/** @brief View size */
		constexpr std::size_t size() const { return _size; }

		/** @brief Whether the view is empty */
		constexpr bool empty() const { return !_size; }

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
		/** @brief Element type */
		typedef const void Type;

		/**
		 * @brief Default constructor
		 *
		 * Creates an empty @cpp nullptr @ce view. Copy a non-empty @ref Array
		 * or @ref ArrayView onto the instance to make it useful.
		 */
		template<class U, class = typename std::enable_if<std::is_same<std::nullptr_t, U>::value>::type> constexpr /*implicit*/ ArrayView(U) noexcept : _data{}, _size{} {}

		constexpr /*implicit*/ ArrayView() noexcept : _data{}, _size{} {}

		/**
		 * @brief Construct a view on a type-erased array array with explicit length
		 * @param data      Data pointer
		 * @param size      Data size in bytes
		 */
		constexpr /*implicit*/ ArrayView(const void* data, std::size_t size) noexcept : _data(data), _size(size) {}

		/**
		 * @brief Constructor a view on a typed array with explicit length
		 * @param data      Data pointer
		 * @param size      Data size
		 *
		 * Size is recalculated to size in bytes.
		 */
		template<class T> constexpr /*implicit*/ ArrayView(const T* data, std::size_t size) noexcept : _data(data), _size(size * sizeof(T)) {}

		/**
		 * @brief Construct a view on a fixed-size array
		 * @param data      Fixed-size array
		 *
		 * Size in bytes is calculated automatically.
		 */
		template<class T, std::size_t size> constexpr /*implicit*/ ArrayView(T(&data)[size]) noexcept : _data(data), _size(size * sizeof(T)) {}

		/** @brief Construct a const void view on an @ref ArrayView<void> */
		constexpr /*implicit*/ ArrayView(ArrayView<void> array) noexcept : _data{array}, _size{array.size()} {}
		
		/** @brief Construct a const void view on any @ref ArrayView */
		template<class T> constexpr /*implicit*/ ArrayView(ArrayView<T> array) noexcept : _data(array), _size(array.size() * sizeof(T)) {}

		/** @brief Construct a const void view on any @ref StaticArrayView */
		template<std::size_t size, class T> constexpr /*implicit*/ ArrayView(const StaticArrayView<size, T>& array) noexcept : _data{array}, _size{size * sizeof(T)} {}

		/** @brief Construct a view on an external type / from an external representation */
		template<class T, class = decltype(Implementation::ErasedArrayViewConverter<const T>::from(std::declval<const T&>()))> constexpr /*implicit*/ ArrayView(const T& other) noexcept : ArrayView{Implementation::ErasedArrayViewConverter<const T>::from(other)} {}

#if !defined(DEATH_MSVC2019_COMPATIBILITY)
		/** @brief Whether the view is non-empty */
		constexpr explicit operator bool() const { return _data; }
#endif

		/** @brief Conversion to the underlying type */
		constexpr /*implicit*/ operator const void*() const { return _data; }

		/** @brief View data */
		constexpr const void* data() const { return _data; }

		/** @brief View size */
		constexpr std::size_t size() const { return _size; }

		/** @brief Whether the view is empty */
		constexpr bool empty() const { return !_size; }

	private:
		const void* _data;
		std::size_t _size;
	};

	/**
		@brief Make a view on an array of specific length

		Convenience alternative to @ref ArrayView::ArrayView(T*, std::size_t).
	*/
	template<class T> constexpr ArrayView<T> arrayView(T* data, std::size_t size) {
		return ArrayView<T>{data, size};
	}

	/**
		@brief Make a view on fixed-size array

		Convenience alternative to @ref ArrayView::ArrayView(U(&)[size]).
	*/
	template<std::size_t size, class T> constexpr ArrayView<T> arrayView(T(&data)[size]) {
		return ArrayView<T>{data};
	}

	/**
		@brief Make a view on an initializer list

		Not present as a constructor in order to avoid accidental dangling references
		with r-value initializer lists.
	*/
	template<class T> ArrayView<const T> arrayView(std::initializer_list<T> list) {
		return ArrayView<const T>{list.begin(), list.size()};
	}

	/**
		@brief Make a view on @ref StaticArrayView

		Convenience alternative to @ref ArrayView::ArrayView(StaticArrayView<size, U>).
	*/
	template<std::size_t size, class T> constexpr ArrayView<T> arrayView(StaticArrayView<size, T> view) {
		return ArrayView<T>{view};
	}

	/**
		@brief Make a view on a view

		Equivalent to the implicit @ref ArrayView copy constructor --- it shouldn't be
		an error to call @ref arrayView() on itself.
	*/
	template<class T> constexpr ArrayView<T> arrayView(ArrayView<T> view) {
		return view;
	}

	/** @brief Make a view on an external type / from an external representation */
	template<class T, class U = decltype(Implementation::ErasedArrayViewConverter<typename std::remove_reference<T&&>::type>::from(std::declval<T&&>()))> constexpr U arrayView(T&& other) {
		return Implementation::ErasedArrayViewConverter<typename std::remove_reference<T&&>::type>::from(std::forward<T>(other));
	}

	/**
		@brief Reinterpret-cast an array view

		Size of the new array is calculated as @cpp view.size()*sizeof(T)/sizeof(U) @ce.
		Expects that both types are [standard layout](http://en.cppreference.com/w/cpp/concept/StandardLayoutType)
		and the total byte size doesn't change.
	*/
	template<class U, class T> ArrayView<U> arrayCast(ArrayView<T> view) {
		static_assert(std::is_standard_layout<T>::value, "The source type is not standard layout");
		static_assert(std::is_standard_layout<U>::value, "The target type is not standard layout");
		const std::size_t size = view.size() * sizeof(T) / sizeof(U);
		DEATH_ASSERT(size * sizeof(U) == view.size() * sizeof(T), {},
			"Containers::arrayCast(): Can't reinterpret %zu %zu-byte items into a %zu-byte type", view.size(), sizeof(T), sizeof(U));
		return { reinterpret_cast<U*>(view.begin()), size };
	}

	/**
		@brief Reinterpret-cast a void array view

		Size of the new array is calculated as @cpp view.size()/sizeof(U) @ce.
		Expects that the target type is [standard layout](http://en.cppreference.com/w/cpp/concept/StandardLayoutType)
		and the total byte size doesn't change.
	*/
	template<class U> ArrayView<U> arrayCast(ArrayView<const void> view) {
		static_assert(std::is_standard_layout<U>::value, "The target type is not standard layout");
		const std::size_t size = view.size() / sizeof(U);
		DEATH_ASSERT(size * sizeof(U) == view.size(), {},
			"Containers::arrayCast(): Can't reinterpret %zu bytes into a %zu-byte type", view.size(), sizeof(U));
		return { reinterpret_cast<U*>(view.data()), size };
	}

	/** @overload */
	template<class U> ArrayView<U> arrayCast(ArrayView<void> view) {
		auto out = arrayCast<const U>(ArrayView<const void>{view});
		return ArrayView<U>{const_cast<U*>(out.data()), out.size()};
	}

	/** @brief Array view size */
	template<class T> constexpr std::size_t arraySize(ArrayView<T> view) {
		return view.size();
	}

	/** @overload */
	template<std::size_t size_, class T> constexpr std::size_t arraySize(StaticArrayView<size_, T>) {
		return size_;
	}

	/** @overload */
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
		/** @brief Element type */
		typedef T Type;

		enum : std::size_t {
			Size = size_
		};

		/**
		 * @brief Default constructor
		 *
		 * Creates a @cpp nullptr @ce view. Copy a non-empty @ref StaticArray
		 * or @ref StaticArrayView onto the instance to make it useful.
		 */
		template<class U, class = U, class = typename std::enable_if<std::is_same<std::nullptr_t, U>::value>::type> constexpr /*implicit*/ StaticArrayView(U) noexcept : _data{} {}

		constexpr /*implicit*/ StaticArrayView() noexcept : _data{} {}

		/**
		* @brief Construct a static view on an array
		* @param data      Data pointer
		*
		* The pointer is assumed to contain at least @ref Size elements, but
		* it can't be checked in any way. Use @ref StaticArrayView(U(&)[size_])
		* or fixed-size slicing from an @ref ArrayView / @ref Array for more
		* safety.
		*/
		template<class U, class = typename std::enable_if<std::is_pointer<U>::value && !std::is_same<U, T(&)[size_]>::value>::type> constexpr explicit StaticArrayView(U data)
			noexcept : _data(data) {}

		/**
		* @brief Construct a static view on a fixed-size array
		* @param data      Fixed-size array
		*
		* Enabled only if @cpp T* @ce is implicitly convertible to @cpp U* @ce.
		* Expects that both types have the same size.
		*/
		template<class U, class = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
		constexpr /*implicit*/ StaticArrayView(U(&data)[size_]) noexcept : _data{data} {}

		/**
		 * @brief Construct a static view on an @ref StaticArrayView
		 *
		 * Enabled only if @cpp T* @ce is implicitly convertible to @cpp U* @ce.
		 * Expects that both types have the same size.
		 */
		template<class U, class = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
		constexpr /*implicit*/ StaticArrayView(StaticArrayView<size_, U> view) noexcept : _data{view} {
			static_assert(sizeof(T) == sizeof(U), "Type sizes are not compatible");
		}

		/** @brief Construct a view on an external type / from an external representation */
		template<class U, class = decltype(Implementation::StaticArrayViewConverter<size_, T, typename std::decay<U&&>::type>::from(std::declval<U&&>()))> constexpr /*implicit*/ StaticArrayView(U&& other) noexcept : StaticArrayView{Implementation::StaticArrayViewConverter<size_, T, typename std::decay<U&&>::type>::from(std::forward<U>(other))} {}

		/** @brief Convert the view to external representation */
		template<class U, class = decltype(Implementation::StaticArrayViewConverter<size_, T, U>::to(std::declval<StaticArrayView<size_, T>>()))> constexpr /*implicit*/ operator U() const {
			return Implementation::StaticArrayViewConverter<size_, T, U>::to(*this);
		}

#if !defined(DEATH_MSVC2019_COMPATIBILITY)
		/** @brief Whether the view is non-empty */
		constexpr explicit operator bool() const { return _data; }
#endif

		/** @brief Conversion to the underlying type */
		constexpr /*implicit*/ operator T*() const { return _data; }

		/** @brief View data */
		constexpr T* data() const { return _data; }

		/** @brief View size */
		constexpr std::size_t size() const { return size_; }

		/** @brief Whether the view is empty */
		constexpr bool empty() const { return !size_; }

		/**
		 * @brief Pointer to the first element
		 *
		 * @see @ref front(), @ref operator[]()
		 */
		constexpr T* begin() const { return _data; }
		/** @overload */
		constexpr T* cbegin() const { return _data; }

		/** @brief Pointer to (one item after) the last element */
		constexpr T* end() const { return _data + size_; }
		/** @overload */
		constexpr T* cend() const { return _data + size_; }

		/**
		 * @brief First element
		 *
		 * Expects there is at least one element.
		 */
		constexpr T& front() const;

		/**
		 * @brief Last element
		 *
		 * Expects there is at least one element.
		 */
		constexpr T& back() const;

		/**
		 * @brief Element access
		 * @m_since_latest
		 *
		 * Expects that @p i is less than @ref size().
		 * @see @ref front(), @ref back()
		 */
		template<class U, class = typename std::enable_if<std::is_convertible<U, std::size_t>::value>::type> constexpr T& operator[](U i) const;

		/** @copydoc ArrayView::slice(T*, T*) const */
		constexpr ArrayView<T> slice(T* begin, T* end) const {
			return ArrayView<T>(*this).slice(begin, end);
		}
		/** @overload */
		constexpr ArrayView<T> slice(std::size_t begin, std::size_t end) const {
			return ArrayView<T>(*this).slice(begin, end);
		}

		/** @copydoc ArrayView::sliceSize(T*, std::size_t) const */
		template<class U, class = typename std::enable_if<std::is_convertible<U, T*>::value && !std::is_convertible<U, std::size_t>::value>::type> constexpr ArrayView<T> sliceSize(U begin, std::size_t size) const {
			return ArrayView<T>(*this).sliceSize(begin, size);
		}
		/** @overload */
		constexpr ArrayView<T> sliceSize(std::size_t begin, std::size_t size) const {
			return ArrayView<T>(*this).sliceSize(begin, size);
		}

		/** @copydoc ArrayView::slice(T*) const */
		template<std::size_t viewSize> constexpr StaticArrayView<viewSize, T> slice(T* begin) const {
			return ArrayView<T>(*this).template slice<viewSize>(begin);
		}
		/** @overload */
		template<std::size_t viewSize> constexpr StaticArrayView<viewSize, T> slice(std::size_t begin) const {
			return ArrayView<T>(*this).template slice<viewSize>(begin);
		}

		/**
		* @brief Fixed-size view slice
		*
		* Expects (at compile time) that @cpp begin_ < end_ @ce and @p end_ is
		* not larger than @ref Size.
		*/
		template<std::size_t begin_, std::size_t end_> constexpr StaticArrayView<end_ - begin_, T> slice() const;

		/**
		 * @brief Fixed-size view slice of given size
		 *
		 * Expects (at compile time) that @cpp begin_ + size_ @ce is not larger
		 * than @ref Size.
		 */
		template<std::size_t begin_, std::size_t size__> constexpr StaticArrayView<size__, T> sliceSize() const {
			return slice<begin_, begin_ + size__>();
		}

		/** @copydoc ArrayView::prefix(T*) const */
		constexpr ArrayView<T> prefix(T* end) const {
			return ArrayView<T>(*this).prefix(end);
		}

		/** @copydoc ArrayView::suffix(T*) const */
		constexpr ArrayView<T> suffix(T* begin) const {
			return ArrayView<T>(*this).suffix(begin);
		}

		/** @copydoc ArrayView::prefix(std::size_t) const */
		constexpr ArrayView<T> prefix(std::size_t size) const {
			return ArrayView<T>(*this).prefix(size);
		}

		/**
		 * @brief Fixed-size view on the first @p size_ items
		 *
		 * Equivalent to @cpp data.slice<0, size_>() @ce.
		 */
		template<std::size_t size__> constexpr StaticArrayView<size__, T> prefix() const {
			return slice<0, size__>();
		}

		/** @copydoc ArrayView::exceptPrefix(std::size_t) const */
		constexpr ArrayView<T> exceptPrefix(std::size_t size__) const {
			return ArrayView<T>(*this).exceptPrefix(size__);
		}

		/**
		 * @brief Fixed-size view except the first @p size__ items
		 *
		 * Equivalent to @cpp data.slice<size__, Size>() @ce.
		 */
		template<std::size_t size__> constexpr StaticArrayView<size_ - size__, T> exceptPrefix() const {
			return slice<size__, size_>();
		}

		/** @copydoc ArrayView::exceptSuffix(std::size_t) const */
		constexpr ArrayView<T> exceptSuffix(std::size_t size) const {
			return ArrayView<T>(*this).exceptSuffix(size);
		}

		/**
		 * @brief Fixed-size view except the last @p size__ items
		 *
		 * Equivalent to @cpp data.slice<0, Size - size__>() @ce.
		 */
		template<std::size_t size__> constexpr StaticArrayView<size_ - size__, T> exceptSuffix() const {
			return slice<0, size_ - size__>();
		}

	private:
#if DEATH_CXX_STANDARD > 201402
		// There doesn't seem to be a way to call those directly, and I can't find any practical use of std::tuple_size,
		// tuple_element etc. on C++11 and C++14, so this is defined only for newer standards.
		template<std::size_t index> constexpr friend T& get(StaticArrayView<size_, T> value) {
			return value._data[index];
		}
		// As the view is non-owning, a rvalue doesn't imply that its contents are able to be moved out. Thus, unlike StaticArray or Pair/Triple,
		// it takes the view by value and has no difference in behavior depending on whether the input is T&, const T& or T&&.
#endif

		T* _data;
	};

	/**
		@brief Make a static view on an array

		Convenience alternative to @ref StaticArrayView::StaticArrayView(T*).
	*/
	template<std::size_t size, class T> constexpr StaticArrayView<size, T> staticArrayView(T* data) {
		return StaticArrayView<size, T>{data};
	}

	/**
		@brief Make a static view on a fixed-size array

		Convenience alternative to @ref StaticArrayView::StaticArrayView(U(&)[size_]).
	*/
	template<std::size_t size, class T> constexpr StaticArrayView<size, T> staticArrayView(T(&data)[size]) {
		return StaticArrayView<size, T>{data};
	}

	/**
		@brief Make a static view on a view

		Equivalent to the implicit @ref StaticArrayView copy constructor --- it
		shouldn't be an error to call @ref staticArrayView() on itself.
	*/
	template<std::size_t size, class T> constexpr StaticArrayView<size, T> staticArrayView(StaticArrayView<size, T> view) {
		return view;
	}

	/** @brief Make a static view on an external type / from an external representation */
	template<class T, class U = decltype(Implementation::ErasedStaticArrayViewConverter<typename std::remove_reference<T&&>::type>::from(std::declval<T&&>()))> constexpr U staticArrayView(T&& other) {
		return Implementation::ErasedStaticArrayViewConverter<typename std::remove_reference<T&&>::type>::from(std::forward<T>(other));
	}

	/**
		@brief Reinterpret-cast a static array view

		Size of the new array is calculated as @cpp view.size()*sizeof(T)/sizeof(U) @ce.
		Expects that both types are [standard layout](http://en.cppreference.com/w/cpp/concept/StandardLayoutType)
		and the total byte size doesn't change.
	*/
	template<class U, std::size_t size, class T> StaticArrayView<size * sizeof(T) / sizeof(U), U> arrayCast(StaticArrayView<size, T> view) {
		static_assert(std::is_standard_layout<T>::value, "The source type is not standard layout");
		static_assert(std::is_standard_layout<U>::value, "The target type is not standard layout");
		constexpr const std::size_t newSize = size * sizeof(T) / sizeof(U);
		static_assert(newSize * sizeof(U) == size * sizeof(T), "Type sizes are not compatible");
		return StaticArrayView<newSize, U>{reinterpret_cast<U*>(view.begin())};
	}

	/**
		@brief Reinterpret-cast a statically sized array

		Calls @ref arrayCast(StaticArrayView<size, T>) with the argument converted to
		@ref StaticArrayView of the same type and size.
	*/
	template<class U, std::size_t size, class T> StaticArrayView<size * sizeof(T) / sizeof(U), U> arrayCast(T(&data)[size]) {
		return arrayCast<U>(StaticArrayView<size, T>{data});
	}

	template<class T> constexpr T& ArrayView<T>::front() const {
		return DEATH_DEBUG_CONSTEXPR_ASSERT(_size != 0, "Containers::ArrayView::front(): View is empty"), _data[0];
	}

	template<class T> constexpr T& ArrayView<T>::back() const {
		return DEATH_DEBUG_CONSTEXPR_ASSERT(_size != 0, "Containers::ArrayView::back(): View is empty"), _data[_size - 1];
	}

	template<class T> template<class U, class> constexpr T& ArrayView<T>::operator[](const U i) const {
		return DEATH_DEBUG_CONSTEXPR_ASSERT(std::size_t(i) < _size, "Containers::ArrayView::operator[](): Index %zu out of range for %zu elements", std::size_t(i), _size),
				_data[i];
	}

	template<class T> constexpr ArrayView<T> ArrayView<T>::slice(T* begin, T* end) const {
		return DEATH_DEBUG_CONSTEXPR_ASSERT(_data <= begin && begin <= end && end <= _data + _size,
					"Containers::ArrayView::slice(): Slice [%zu:%zu] out of range for %zu elements",
					std::size_t(begin - _data), std::size_t(end - _data), _size),
				ArrayView<T>{begin, std::size_t(end - begin)};
	}

	template<class T> constexpr ArrayView<T> ArrayView<T>::slice(std::size_t begin, std::size_t end) const {
		return DEATH_DEBUG_CONSTEXPR_ASSERT(begin <= end && end <= _size,
					"Containers::ArrayView::slice(): Slice [%zu:%zu] out of range for %zu elements", begin, end, _size),
				ArrayView<T>{_data + begin, end - begin};
	}

	template<std::size_t size_, class T> constexpr T& StaticArrayView<size_, T>::front() const {
		static_assert(size_ != 0, "View is empty");
		return _data[0];
	}

	template<std::size_t size_, class T> constexpr T& StaticArrayView<size_, T>::back() const {
		static_assert(size_ != 0, "View is empty");
		return _data[size_ - 1];
	}

	template<std::size_t size_, class T> template<class U, class> constexpr T& StaticArrayView<size_, T>::operator[](const U i) const {
		return DEATH_DEBUG_CONSTEXPR_ASSERT(std::size_t(i) < size_, "Containers::ArrayView::operator[](): Index %zu out of range for %zu elements", std::size_t(i), size_),
				_data[i];
	}

	template<class T> template<std::size_t size_, class U, class> constexpr StaticArrayView<size_, T> ArrayView<T>::slice(const U begin) const {
		return DEATH_DEBUG_CONSTEXPR_ASSERT(_data <= begin && begin + size_ <= _data + _size,
					"Containers::ArrayView::slice(): Slice [%zu:%zu] out of range for %zu elements",
					std::size_t(begin - _data), std::size_t(begin + size_ - _data), _size),
				StaticArrayView<size_, T>{begin};
	}

	template<class T> template<std::size_t size_> constexpr StaticArrayView<size_, T> ArrayView<T>::slice(std::size_t begin) const {
		static_assert(begin + size_ <= _size, "Slice needs to have a positive size");
		return DEATH_DEBUG_CONSTEXPR_ASSERT(begin + size_ <= _size,
					"Containers::ArrayView::slice(): Slice [%zu:%zu] out of range for %zu elements", begin_, begin + size_, _size),
				StaticArrayView<size_, T>{_data + begin};
	}

	template<class T> template<std::size_t begin_, std::size_t end_> constexpr StaticArrayView<end_ - begin_, T> ArrayView<T>::slice() const {
		static_assert(begin_ < end_, "Fixed-size slice needs to have a positive size");
		return DEATH_DEBUG_CONSTEXPR_ASSERT(end_ <= _size,
					"Containers::ArrayView::slice(): Slice [%zu:%zu] out of range for %zu elements", begin_, end_, _size),
				StaticArrayView<end_ - begin_, T>{_data + begin_};
	}

	template<std::size_t size_, class T> template<std::size_t begin_, std::size_t end_> constexpr StaticArrayView<end_ - begin_, T> StaticArrayView<size_, T>::slice() const {
		static_assert(begin_ < end_, "Fixed-size slice needs to have a positive size");
		static_assert(end_ <= size_, "Slice out of range");
		return StaticArrayView<end_ - begin_, T>{_data + begin_};
	}
}}

/* C++17 structured bindings */
#if DEATH_CXX_STANDARD > 201402
namespace std
{
	// Note that `size` can't be used as it may conflict with std::size() in C++17
	template<size_t size_, class T> struct tuple_size<Death::Containers::StaticArrayView<size_, T>> : integral_constant<size_t, size_> {};
	template<size_t index, size_t size_, class T> struct tuple_element<index, Death::Containers::StaticArrayView<size_, T>> { typedef T type; };
}
#endif