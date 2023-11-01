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
		/** @brief Element type */
		typedef T Type;

		enum : std::size_t {
			Size = size_
		};

		/**
		 * @brief Construct a default-initialized array
		 *
		 * Creates array of given size, the contents are default-initialized
		 * (i.e., trivial types are not initialized). Because of the differing
		 * behavior for trivial types it's better to explicitly use either the
		 * @ref StaticArray(ValueInitT) or the @ref StaticArray(NoInitT)
		 * variant instead.
		 */
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

		/**
		 * @brief Construct a value-initialized array
		 *
		 * Creates array of given size, the contents are value-initialized
		 * (i.e., trivial types are zero-initialized, default constructor
		 * called otherwise). This is the same as @ref StaticArray().
		 */
		explicit StaticArray(ValueInitT) : _data() {}

		/**
		* @brief Construct an array without initializing its contents
		*
		* Creates array of given size, the contents are *not* initialized.
		* Useful if you will be overwriting all elements later anyway or if
		* you need to call custom constructors in a way that's not expressible
		* via any other @ref StaticArray constructor.
		*
		* For trivial types is equivalent to @ref StaticArray(DefaultInitT).
		* For non-trivial types, the class will explicitly call the destructor
		* on *all elements* --- which means that for non-trivial types you're
		* expected to construct all elements using placement new (or for
		* example @ref std::uninitialized_copy()) in order to avoid calling
		* destructors on uninitialized memory.
		*/
		explicit StaticArray(NoInitT) {}

		/**
		 * @brief Construct a direct-initialized array
		 *
		 * Constructs the array using the @ref StaticArray(NoInitT) constructor
		 * and then initializes each element with placement new using forwarded
		 * @p args.
		 */
		template<class ...Args> explicit StaticArray(DirectInitT, Args&&... args);

		/**
		 * @brief Construct an in-place-initialized array
		 *
		 * The arguments are forwarded to the array constructor. Same as
		 * @ref StaticArray(Args&&... args).
		 */
		template<class ...Args> explicit StaticArray(InPlaceInitT, Args&&... args) : _data{std::forward<Args>(args)...} {
			static_assert(sizeof...(args) == size_, "Containers::StaticArray: Wrong number of initializers");
		}

		/**
		 * @brief Construct a value-initialized array
		 *
		 * Alias to @ref StaticArray(ValueInitT).
		 */
		explicit StaticArray() : StaticArray{ValueInit} {}

		/**
		 * @brief Construct an in-place-initialized array
		 *
		 * Alias to @ref StaticArray(InPlaceInitT, Args&&... args).
		 */
		template<class First, class ...Next, class = typename std::enable_if<std::is_convertible<First&&, T>::value>::type> /*implicit*/ StaticArray(First&& first, Next&&... next) : StaticArray{InPlaceInit, std::forward<First>(first), std::forward<Next>(next)...} {}

		/** @brief Copy constructor */
		StaticArray(const StaticArray<size_, T>& other) noexcept(std::is_nothrow_copy_constructible<T>::value);

		/** @brief Move constructor */
		StaticArray(StaticArray<size_, T>&& other) noexcept(std::is_nothrow_move_constructible<T>::value);

		~StaticArray();

		/** @brief Copy assignment */
		StaticArray<size_, T>& operator=(const StaticArray<size_, T>&) noexcept(std::is_nothrow_copy_constructible<T>::value);

		/** @brief Move assignment */
		StaticArray<size_, T>& operator=(StaticArray<size_, T>&&) noexcept(std::is_nothrow_move_constructible<T>::value);

		/** @brief Convert to external view representation */
		template<class U, class = decltype(Implementation::StaticArrayViewConverter<size_, T, U>::to(std::declval<StaticArrayView<size_, T>>()))> /*implicit*/ operator U() {
			return Implementation::StaticArrayViewConverter<size_, T, U>::to(*this);
		}

		/** @overload */
		template<class U, class = decltype(Implementation::StaticArrayViewConverter<size_, const T, U>::to(std::declval<StaticArrayView<size_, const T>>()))> constexpr /*implicit*/ operator U() const {
			return Implementation::StaticArrayViewConverter<size_, const T, U>::to(*this);
		}

		/** @brief Conversion to array type */
		/*implicit*/ operator T*() & {
			return _data;
		}

		/** @overload */
		/*implicit*/ operator const T*() const & {
			return _data;
		}

		/** @brief Array data */
		T* data() { return _data; }
		const T* data() const { return _data; }				/**< @overload */

		/**
		 * @brief Array size
		 *
		 * Equivalent to @ref Size.
		 */
		constexpr std::size_t size() const { return size_; }

		/**
		 * @brief Whether the array is empty
		 *
		 * Always @cpp true @ce (it's not possible to create a zero-sized C
		 * array).
		 */
		constexpr bool empty() const { return !size_; }

		/** @brief Pointer to the first element */
		T* begin() { return _data; }
		const T* begin() const { return _data; }			/**< @overload */
		const T* cbegin() const { return _data; }			/**< @overload */

		/** @brief Pointer to (one item after) the last element */
		T* end() { return _data + size_; }
		const T* end() const { return _data + size_; }		/**< @overload */
		const T* cend() const { return _data + size_; }		/**< @overload */

		/** @brief First element */
		T& front() { return _data[0]; }
		const T& front() const { return _data[0]; }			/**< @overload */

		/** @brief Last element */
		T& back() { return _data[size_ - 1]; }
		const T& back() const { return _data[size_ - 1]; }	/**< @overload */

		/**
		 * @brief Element access
		 *
		 * Expects that @p i is less than @ref size().
		 */
		template<class U, class = typename std::enable_if<std::is_convertible<U, std::size_t>::value>::type> T& operator[](U i);
		template<class U, class = typename std::enable_if<std::is_convertible<U, std::size_t>::value>::type> const T& operator[](U i) const;

		/**
		 * @brief View on a slice
		 *
		 * Equivalent to @ref StaticArrayView::slice(T*, T*) const and
		 * overloads.
		 */
		ArrayView<T> slice(T* begin, T* end) {
			return ArrayView<T>(*this).slice(begin, end);
		}
		/** @overload */
		ArrayView<const T> slice(const T* begin, const T* end) const {
			return ArrayView<const T>(*this).slice(begin, end);
		}
		/** @overload */
		ArrayView<T> slice(std::size_t begin, std::size_t end) {
			return ArrayView<T>(*this).slice(begin, end);
		}
		/** @overload */
		ArrayView<const T> slice(std::size_t begin, std::size_t end) const {
			return ArrayView<const T>(*this).slice(begin, end);
		}

		/**
		 * @brief View on a slice of given size
		 *
		 * Equivalent to @ref StaticArrayView::sliceSize(T*, std::size_t) const
		 * and overloads.
		 */
		template<class U, class = typename std::enable_if<std::is_convertible<U, T*>::value && !std::is_convertible<U, std::size_t>::value>::type> ArrayView<T> sliceSize(U begin, std::size_t size) {
			return ArrayView<T>{*this}.sliceSize(begin, size);
		}
		/** @overload */
		template<class U, class = typename std::enable_if<std::is_convertible<U, const T*>::value && !std::is_convertible<U, std::size_t>::value>::type> ArrayView<const T> sliceSize(const U begin, std::size_t size) const {
			return ArrayView<const T>{*this}.sliceSize(begin, size);
		}
		/** @overload */
		ArrayView<T> sliceSize(std::size_t begin, std::size_t size) {
			return ArrayView<T>{*this}.sliceSize(begin, size);
		}
		/** @overload */
		ArrayView<const T> sliceSize(std::size_t begin, std::size_t size) const {
			return ArrayView<const T>{*this}.sliceSize(begin, size);
		}

		/**
		 * @brief Fixed-size view on a slice
		 *
		 * Equivalent to @ref StaticArrayView::slice(T*) const and overloads.
		 */
		template<std::size_t size__, class U, class = typename std::enable_if<std::is_convertible<U, T*>::value && !std::is_convertible<U, std::size_t>::value>::type> StaticArrayView<size__, T> slice(U begin) {
			return ArrayView<T>(*this).template slice<size__>(begin);
		}
		/** @overload */
		template<std::size_t size__, class U, class = typename std::enable_if<std::is_convertible<U, const T*>::value && !std::is_convertible<U, std::size_t>::value>::type> StaticArrayView<size__, const T> slice(U begin) const {
			return ArrayView<const T>(*this).template slice<size__>(begin);
		}
		/** @overload */
		template<std::size_t size__> StaticArrayView<size__, T> slice(std::size_t begin) {
			return ArrayView<T>(*this).template slice<size__>(begin);
		}
		/** @overload */
		template<std::size_t size__> StaticArrayView<size__, const T> slice(std::size_t begin) const {
			return ArrayView<const T>(*this).template slice<size__>(begin);
		}

		/**
		 * @brief Fixed-size view on a slice
		 *
		 * Equivalent to @ref StaticArrayView::slice() const.
		 */
		template<std::size_t begin_, std::size_t end_> StaticArrayView<end_ - begin_, T> slice() {
			return StaticArrayView<size_, T>(*this).template slice<begin_, end_>();
		}
		/** @overload */
		template<std::size_t begin_, std::size_t end_> StaticArrayView<end_ - begin_, const T> slice() const {
			return StaticArrayView<size_, const T>(*this).template slice<begin_, end_>();
		}

		/**
		 * @brief Fixed-size view on a slice of given size
		 *
		 * Equivalent to @ref StaticArrayView::sliceSize() const.
		 */
		template<std::size_t begin_, std::size_t size__> StaticArrayView<size__, T> sliceSize() {
			return StaticArrayView<size_, T>(*this).template sliceSize<begin_, size__>();
		}
		/** @overload */
		template<std::size_t begin_, std::size_t size__> StaticArrayView<size__, const T> sliceSize() const {
			return StaticArrayView<size_, const T>(*this).template sliceSize<begin_, size__>();
		}

		/**
		 * @brief View on a prefix until a pointer
		 *
		 * Equivalent to @ref StaticArrayView::prefix(T*) const.
		 */
		template<class U, class = typename std::enable_if<std::is_convertible<U, T*>::value && !std::is_convertible<U, std::size_t>::value>::type>
		ArrayView<T> prefix(U end) {
			return ArrayView<T>(*this).prefix(end);
		}
		/** @overload */
		template<class U, class = typename std::enable_if<std::is_convertible<U, const T*>::value && !std::is_convertible<U, std::size_t>::value>::type>
		ArrayView<const T> prefix(U end) const {
			return ArrayView<const T>(*this).prefix(end);
		}

		/**
		 * @brief View on a suffix after a pointer
		 *
		 * Equivalent to @ref StaticArrayView::suffix(T*) const.
		 */
		ArrayView<T> suffix(T* begin) {
			return ArrayView<T>(*this).suffix(begin);
		}
		/** @overload */
		ArrayView<const T> suffix(const T* begin) const {
			return ArrayView<const T>(*this).suffix(begin);
		}

		/**
		 * @brief View on the first @p size items
		 *
		 * Equivalent to @ref StaticArrayView::prefix(std::size_t) const.
		 */
		ArrayView<T> prefix(std::size_t size) {
			return ArrayView<T>(*this).prefix(size);
		}
		/** @overload */
		ArrayView<const T> prefix(std::size_t size) const {
			return ArrayView<const T>(*this).prefix(size);
		}

		/**
		* @brief Fixed-size view on the first @p size__ items
		*
		* Equivalent to @ref StaticArrayView::prefix() const and overloads.
		*/
		template<std::size_t size__> StaticArrayView<size__, T> prefix();
		/** @overload */
		template<std::size_t size__> StaticArrayView<size__, const T> prefix() const;

		/**
		 * @brief View except the first @p size items
		 *
		 * Equivalent to @ref StaticArrayView::exceptPrefix(std::size_t) const.
		 */
		ArrayView<T> exceptPrefix(std::size_t size) {
			return ArrayView<T>(*this).exceptPrefix(size);
		}
		/** @overload */
		ArrayView<const T> exceptPrefix(std::size_t size) const {
			return ArrayView<const T>(*this).exceptPrefix(size);
		}

		/**
		* @brief Fixed-size view except the first @p size__ items
		*
		* Equivalent to @ref StaticArrayView::exceptPrefix() const and
		* overloads.
		*/
		template<std::size_t size__> StaticArrayView<size_ - size__, T> exceptPrefix() {
			return StaticArrayView<size_, T>(*this).template exceptPrefix<size__>();
		}
		/** @overload */
		template<std::size_t size__> StaticArrayView<size_ - size__, const T> exceptPrefix() const {
			return StaticArrayView<size_, const T>(*this).template exceptPrefix<size__>();
		}

		/**
		 * @brief View except the last @p size items
		 *
		 * Equivalent to @ref StaticArrayView::exceptSuffix(std::size_t) const.
		 */
		ArrayView<T> exceptSuffix(std::size_t size) {
			return ArrayView<T>(*this).exceptSuffix(size);
		}
		/** @overload */
		ArrayView<const T> exceptSuffix(std::size_t size) const {
			return ArrayView<const T>(*this).exceptSuffix(size);
		}

		/**
		 * @brief Fixed-size view except the last @p size__ items
		 *
		 * Equivalent to @ref StaticArrayView::exceptSuffix() const.
		 */
		template<std::size_t size__> StaticArrayView<size_ - size__, T> exceptSuffix() {
			return StaticArrayView<size_, T>(*this).template exceptSuffix<size__>();
		}
		/** @overload */
		template<std::size_t size__> StaticArrayView<size_ - size__, const T> exceptSuffix() const {
			return StaticArrayView<size_, const T>(*this).template exceptSuffix<size__>();
		}

	private:
		union {
			T _data[size_];
		};
	};

	/**
		@brief Make a view on a @ref StaticArray

		Convenience alternative to converting to an @ref ArrayView explicitly.
	*/
	template<std::size_t size, class T> constexpr ArrayView<T> arrayView(StaticArray<size, T>& array) {
		return ArrayView<T>{array};
	}

	/**
		@brief Make a view on a const @ref StaticArray

		Convenience alternative to converting to an @ref ArrayView explicitly.
	*/
	template<std::size_t size, class T> constexpr ArrayView<const T> arrayView(const StaticArray<size, T>& array) {
		return ArrayView<const T>{array};
	}

	/**
		@brief Make a static view on a @ref StaticArray

		Convenience alternative to converting to an @ref StaticArrayView explicitly.
	*/
	template<std::size_t size, class T> constexpr StaticArrayView<size, T> staticArrayView(StaticArray<size, T>& array) {
		return StaticArrayView<size, T>{array};
	}

	/**
		@brief Make a static view on a const @ref StaticArray

		Convenience alternative to converting to an @ref StaticArrayView explicitly.
	*/
	template<std::size_t size, class T> constexpr StaticArrayView<size, const T> staticArrayView(const StaticArray<size, T>& array) {
		return StaticArrayView<size, const T>{array};
	}

	/**
		@brief Reinterpret-cast a static array

		See @ref arrayCast(StaticArrayView<size, T>) for more information.
	*/
	template<class U, std::size_t size, class T> StaticArrayView<size * sizeof(T) / sizeof(U), U> arrayCast(StaticArray<size, T>& array) {
		return arrayCast<U>(staticArrayView(array));
	}

	/** @overload */
	template<class U, std::size_t size, class T> StaticArrayView<size * sizeof(T) / sizeof(U), const U> arrayCast(const StaticArray<size, T>& array) {
		return arrayCast<const U>(staticArrayView(array));
	}

	/**
		@brief Static array size

		See @ref arraySize(ArrayView<T>) for more information.
	*/
	template<std::size_t size_, class T> constexpr std::size_t arraySize(const StaticArray<size_, T>&) {
		return size_;
	}

	template<std::size_t size_, class T> template<class ...Args> StaticArray<size_, T>::StaticArray(DirectInitT, Args&&... args) : StaticArray { NoInit } {
		for (T& i : _data) {
			Implementation::construct(i, std::forward<Args>(args)...);
		}
	}

	template<std::size_t size_, class T> StaticArray<size_, T>::StaticArray(const StaticArray<size_, T>& other) noexcept(std::is_nothrow_copy_constructible<T>::value) : StaticArray { NoInit } {
		for (std::size_t i = 0; i != other.size(); ++i)
			// Can't use {}, see the GCC 4.8-specific overload for details
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ < 5
			Implementation::construct(_data[i], other._data[i]);
#else
			new(_data + i) T{other._data[i]};
#endif
	}

	template<std::size_t size_, class T> StaticArray<size_, T>::StaticArray(StaticArray<size_, T>&& other) noexcept(std::is_nothrow_move_constructible<T>::value) : StaticArray { NoInit } {
		for (std::size_t i = 0; i != other.size(); ++i)
			// Can't use {}, see the GCC 4.8-specific overload for details
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ < 5
			Implementation::construct(_data[i], std::move(other._data[i]));
#else
			new(&_data[i]) T{std::move(other._data[i])};
#endif
	}

	template<std::size_t size_, class T> StaticArray<size_, T>::~StaticArray() {
		for (T& i : _data) {
			i.~T();
#if defined(DEATH_MSVC2015_COMPATIBILITY)
			// Complains i is set but not used for trivially destructible types
			static_cast<void>(i);
#endif
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

	template<std::size_t size_, class T> template<class U, class> const T& StaticArray<size_, T>::operator[](const U i) const {
		return _data[i];
	}

	template<std::size_t size_, class T> template<class U, class> T& StaticArray<size_, T>::operator[](const U i) {
		return const_cast<T&>(static_cast<const StaticArray<size_, T>&>(*this)[i]);
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