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
#include "ArrayView.h"
#include "Tags.h"

#include <initializer_list>
#include <new>
#include <type_traits>
#include <utility>

namespace Death { namespace Containers {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	// Forward declarations for the Death::Containers namespace
	template<class T, class = void(*)(T*, std::size_t)> class Array;

	namespace Implementation
	{
		template<class T, class First, class ...Next> inline void construct(T& value, First&& first, Next&& ...next) {
			new(&value) T{std::forward<First>(first), std::forward<Next>(next)...};
		}
		template<class T> inline void construct(T& value) {
			new(&value) T();
		}

#if defined(DEATH_TARGET_GCC) && __GNUC__ < 5
		template<class T> inline void construct(T& value, T&& b) {
			new(&value) T(std::move(b));
		}
#endif

		template<class T, class D> struct CallDeleter {
			void operator()(D deleter, T* data, std::size_t size) const {
				deleter(data, size);
			}
		};
		template<class T> struct CallDeleter<T, void(*)(T*, std::size_t)> {
			void operator()(void(*deleter)(T*, std::size_t), T* data, std::size_t size) const {
				if (deleter) deleter(data, size);
				else delete[] data;
			}
		};

		template<class T, typename std::enable_if<std::is_trivial<T>::value, int>::type = 0> T* noInitAllocate(std::size_t size) {
			return new T[size];
		}
		template<class T, typename std::enable_if<!std::is_trivial<T>::value, int>::type = 0> T* noInitAllocate(std::size_t size) {
			return reinterpret_cast<T*>(new char[size * sizeof(T)]);
		}

		template<class T, typename std::enable_if<std::is_trivial<T>::value, int>::type = 0> auto noInitDeleter() -> void(*)(T*, std::size_t) {
			return nullptr; // Using the default deleter for T
		}
		template<class T, typename std::enable_if<!std::is_trivial<T>::value, int>::type = 0> auto noInitDeleter() -> void(*)(T*, std::size_t) {
			return [](T* data, std::size_t size) {
				if (data) for (T* it = data, *end = data + size; it != end; ++it)
					it->~T();
				delete[] reinterpret_cast<char*>(data);
			};
		}
	}

	/**
		@brief Array
		@tparam T   Element type
		@tparam D   Deleter type. Defaults to pointer to a @cpp void(T*, std::size_t) @ce function, where first is array pointer and second array size.

		A RAII owning wrapper around a plain C array. A lighter alternative to @ref std::vector that's deliberately
		move-only to avoid accidental copies of large memory blocks. For a variant with compile-time size information
		see @ref StaticArray. A non-owning version of this container is an @ref ArrayView.

		The array has a non-changeable size by default and growing functionality is opt-in.
	*/
	template<class T, class D> class Array
	{
	public:
		/** @brief Element type */
		typedef T Type;

		/**
		 * @brief Deleter type
		 *
		 * Defaults to pointer to a @cpp void(T*, std::size_t) @ce function,
		 * where first is array pointer and second array size.
		 */
		typedef D Deleter;

		/**
		 * @brief Default constructor
		 *
		 * Creates a zero-sized array. Move an @ref Array with a nonzero size
		 * onto the instance to make it useful.
		 */
		template<class U, class = typename std::enable_if<std::is_same<std::nullptr_t, U>::value>::type> /*implicit*/ Array(U) noexcept : _data{nullptr}, _size{0}, _deleter{} {}

		/*implicit*/ Array() noexcept : _data(nullptr), _size(0), _deleter{} {}

		/**
		 * @brief Construct a default-initialized array
		 *
		 * Creates an array of given size, the contents are default-initialized
		 * (i.e. trivial types are not initialized, default constructor called
		 * otherwise). If the size is zero, no allocation is done. Because of
		 * the differing behavior for trivial types it's better to explicitly
		 * use either the @ref Array(ValueInitT, std::size_t) or the
		 * @ref Array(NoInitT, std::size_t) variant instead.
		 */
		explicit Array(DefaultInitT, std::size_t size) : _data{size ? new T[size] : nullptr}, _size{size}, _deleter{nullptr} {}

		/**
		 * @brief Construct a value-initialized array
		 *
		 * Creates an array of given size, the contents are value-initialized
		 * (i.e. trivial types are zero-initialized, default constructor called
		 * otherwise). This is the same as @ref Array(std::size_t). If the size
		 * is zero, no allocation is done.
		 */
		explicit Array(ValueInitT, std::size_t size) : _data{size ? new T[size]() : nullptr}, _size{size}, _deleter{nullptr} {}

		/**
		 * @brief Construct an array without initializing its contents
		 *
		 * Creates an array of given size, the contents are *not* initialized.
		 * If the size is zero, no allocation is done. Useful if you will be
		 * overwriting all elements later anyway or if you need to call custom
		 * constructors in a way that's not expressible via any other
		 * @ref Array constructor.
		 *
		 * For trivial types is equivalent to @ref Array(DefaultInitT, std::size_t),
		 * with @ref deleter() being the default (@cpp nullptr @ce) as well.
		 * For non-trivial types, the data are allocated as a @cpp char @ce
		 * array. Destruction is done using a custom deleter that explicitly
		 * calls the destructor on *all elements* and then deallocates the data
		 * as a @cpp char @ce array again --- which means that for non-trivial
		 * types you're expected to construct all elements using placement new
		 * (or for example @ref std::uninitialized_copy()) in order to avoid
		 * calling destructors on uninitialized memory.
		 */
		explicit Array(NoInitT, std::size_t size) : _data{size ? Implementation::noInitAllocate<T>(size) : nullptr}, _size{size}, _deleter{Implementation::noInitDeleter<T>()} {}

		/**
		 * @brief Construct a direct-initialized array
		 *
		 * Allocates the array using the @ref Array(NoInitT, std::size_t)
		 * constructor and then initializes each element with placement new
		 * using forwarded @p args.
		 */
		template<class ...Args> explicit Array(DirectInitT, std::size_t size, Args&&... args);

		/**
		 * @brief Construct a list-initialized array
		 *
		 * Allocates the array using the @ref Array(NoInitT, std::size_t)
		 * constructor and then copy-initializes each element with placement
		 * new using values from @p list. To save typing you can also use the
		 * @ref array(ArrayView<const T>) /
		 * @ref array(std::initializer_list<T>) shorthands.
		 */
		/*implicit*/ Array(InPlaceInitT, ArrayView<const T> list);

		/** @overload */
		/*implicit*/ Array(InPlaceInitT, std::initializer_list<T> list);

		/**
		 * @brief Construct a value-initialized array
		 *
		 * Alias to @ref Array(ValueInitT, std::size_t).
		 */
		explicit Array(std::size_t size) : Array{ValueInit, size} {}

		/**
		 * @brief Wrap an existing array with an explicit deleter
		 *
		 * The @p deleter will be *unconditionally* called on destruction with
		 * @p data and @p size as an argument. In particular, it will be also
		 * called if @p data is @cpp nullptr @ce or @p size is @cpp 0 @ce.
		 *
		 * In case of a moved-out instance, the deleter gets reset to a
		 * default-constructed value alongside the array pointer and size. For
		 * plain deleter function pointers it effectively means
		 * @cpp delete[] nullptr @ce gets called when destructing a moved-out
		 * instance (which is a no-op), for stateful deleters you have to
		 * ensure the deleter similarly does nothing in its default state.
		 */
		explicit Array(T* data, std::size_t size, D deleter = {}) noexcept : _data{data}, _size{size}, _deleter(deleter) {}

		/**
		* @brief Wrap an existing array view with an explicit deleter
		*
		* Convenience overload of @ref Array(T*, std::size_t, D) for cases
		* where the pointer and size is already wrapped in an @ref ArrayView,
		* such as when creating non-owned @ref Array instances.
		*/
		explicit Array(ArrayView<T> view, D deleter) noexcept : Array{view.data(), view.size(), deleter} {}

		/** @brief Copying is not allowed */
		Array(const Array<T, D>&) = delete;

		/**
		 * @brief Move constructor
		 *
		 * Resets data pointer, size and deleter of @p other to be equivalent
		 * to a default-constructed instance.
		 */
		Array(Array<T, D>&& other) noexcept;

		/**
		 * @brief Destructor
		 *
		 * Calls @ref deleter() on the owned @ref data().
		 */
		~Array() { Implementation::CallDeleter<T, D>{}(_deleter, _data, _size); }

		/** @brief Copying is not allowed */
		Array<T, D>& operator=(const Array<T, D>&) = delete;

		/**
		 * @brief Move assignment
		 *
		 * Swaps data pointer, size and deleter of the two instances.
		 */
		Array<T, D>& operator=(Array<T, D>&& other) noexcept;

		/** @brief Convert to external view representation */
		template<class U, class = decltype(Implementation::ArrayViewConverter<T, U>::to(std::declval<ArrayView<T>>()))> /*implicit*/ operator U() {
			return Implementation::ArrayViewConverter<T, U>::to(*this);
		}

		/** @overload */
		template<class U, class = decltype(Implementation::ArrayViewConverter<const T, U>::to(std::declval<ArrayView<const T>>()))> constexpr /*implicit*/ operator U() const {
			return Implementation::ArrayViewConverter<const T, U>::to(*this);
		}

#if !defined(DEATH_MSVC2019_COMPATIBILITY)
		/** @brief Whether the array is non-empty */
		explicit operator bool() const {
			return _data;
		}
#endif

		/** @brief Conversion to array type */
		/*implicit*/ operator T*() & { return _data; }

		/** @overload */
		/*implicit*/ operator const T*() const & { return _data; }

		/** @brief Array data */
		T* data() { return _data; }
		const T* data() const { return _data; }			/**< @overload */

		/**
		 * @brief Array deleter
		 *
		 * If set to @cpp nullptr @ce, the contents are deleted using standard
		 * @cpp operator delete[] @ce.
		 * @see @ref Array(T*, std::size_t, D)
		 */
		D deleter() const { return _deleter; }

		/**
		 * @brief Array size
		 *
		 * @see @ref isEmpty()
		 */
		std::size_t size() const { return _size; }

		/**
		 * @brief Whether the array is empty
		 * @m_since_latest
		 *
		 * @see @ref size()
		 */
		bool empty() const { return !_size; }

		/**
		 * @brief Pointer to first element
		 *
		 * @see @ref front(), @ref operator[]()
		 */
		T* begin() { return _data; }
		const T* begin() const { return _data; }		/**< @overload */
		const T* cbegin() const { return _data; }		/**< @overload */

		/**
		 * @brief Pointer to (one item after) last element
		 *
		 * @see @ref back(), @ref operator[]()
		 */
		T* end() { return _data+_size; }
		const T* end() const { return _data+_size; }	/**< @overload */
		const T* cend() const { return _data+_size; }	/**< @overload */

		/**
		 * @brief First element
		 *
		 * Expects there is at least one element.
		 * @see @ref begin(), @ref operator[]()
		 */
		T& front();
		const T& front() const;							/**< @overload */

		/**
		 * @brief Last element
		 *
		 * Expects there is at least one element.
		 * @see @ref end(), @ref operator[]()
		 */
		T& back();
		const T& back() const;							/**< @overload */

		// Has to be done this way because otherwise it causes ambiguity with a builtin operator[] for pointers if an int
		// or ssize_t is used due to the implicit pointer conversion. Sigh.
		template<class U, class = typename std::enable_if<std::is_convertible<U, std::size_t>::value>::type> T& operator[](U i);
		template<class U, class = typename std::enable_if<std::is_convertible<U, std::size_t>::value>::type> const T& operator[](U i) const;

		/**
		 * @brief View on a slice
		 *
		 * Equivalent to @ref ArrayView::slice(T*, T*) const and overloads.
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
		 * @m_since_latest
		 *
		 * Equivalent to @ref ArrayView::sliceSize(T*, std::size_t) const and
		 * overloads.
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
		 * Equivalent to @ref ArrayView::slice(T*) const and overloads.
		 */
		template<std::size_t size_, class U, class = typename std::enable_if<std::is_convertible<U, T*>::value && !std::is_convertible<U, std::size_t>::value>::type> StaticArrayView<size_, T> slice(U begin) {
			return ArrayView<T>(*this).template slice<size_>(begin);
		}
		/** @overload */
		template<std::size_t size_, class U, class = typename std::enable_if<std::is_convertible<U, const T*>::value && !std::is_convertible<U, std::size_t>::value>::type> StaticArrayView<size_, const T> slice(U begin) const {
			return ArrayView<const T>(*this).template slice<size_>(begin);
		}
		/** @overload */
		template<std::size_t size_> StaticArrayView<size_, T> slice(std::size_t begin) {
			return ArrayView<T>(*this).template slice<size_>(begin);
		}
		/** @overload */
		template<std::size_t size_> StaticArrayView<size_, const T> slice(std::size_t begin) const {
			return ArrayView<const T>(*this).template slice<size_>(begin);
		}

		/**
		 * @brief Fixed-size view on a slice
		 *
		 * Equivalent to @ref ArrayView::slice() const.
		 */
		template<std::size_t begin_, std::size_t end_> StaticArrayView<end_ - begin_, T> slice() {
			return ArrayView<T>(*this).template slice<begin_, end_>();
		}
		/** @overload */
		template<std::size_t begin_, std::size_t end_> StaticArrayView<end_ - begin_, const T> slice() const {
			return ArrayView<const T>(*this).template slice<begin_, end_>();
		}

		/**
		 * @brief View on a prefix until a pointer
		 *
		 * Equivalent to @ref ArrayView::prefix(T*) const.
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
		 * Equivalent to @ref ArrayView::suffix(T*) const.
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
		 * Equivalent to @ref ArrayView::prefix(std::size_t) const.
		 */
		ArrayView<T> prefix(std::size_t end) {
			return ArrayView<T>(*this).prefix(end);
		}
		/** @overload */
		ArrayView<const T> prefix(std::size_t end) const {
			return ArrayView<const T>(*this).prefix(end);
		}

		/**
		 * @brief Fixed-size view on the first @p size_ items
		 *
		 * Equivalent to @ref ArrayView::prefix() const.
		 */
		template<std::size_t viewSize_> StaticArrayView<viewSize_, T> prefix() {
			return ArrayView<T>(*this).template prefix<viewSize_>();
		}
		/** @overload */
		template<std::size_t viewSize_> StaticArrayView<viewSize_, const T> prefix() const {
			return ArrayView<const T>(*this).template prefix<viewSize_>();
		}

		/**
		 * @brief Fixed-size view on the last @p size_ items
		 *
		 * Equivalent to @ref ArrayView::suffix() const.
		 */
		template<std::size_t size_> StaticArrayView<size_, T> suffix() {
			return ArrayView<T>(*this).template suffix<size_>();
		}
		/** @overload */
		template<std::size_t size_> StaticArrayView<size_, const T> suffix() const {
			return ArrayView<const T>(*this).template suffix<size_>();
		}

		/**
		 * @brief View except the first @p size_ items
		 *
		 * Equivalent to @ref ArrayView::exceptPrefix(std::size_t) const.
		 */
		ArrayView<T> exceptPrefix(std::size_t size_) {
			return ArrayView<T>(*this).exceptPrefix(size_);
		}
		/** @overload */
		ArrayView<const T> exceptPrefix(std::size_t size_) const {
			return ArrayView<const T>(*this).exceptPrefix(size_);
		}

		/**
		 * @brief View except the last @p size items
		 *
		 * Equivalent to @ref ArrayView::exceptSuffix().
		 */
		ArrayView<T> exceptSuffix(std::size_t size) {
			return ArrayView<T>(*this).exceptSuffix(size);
		}
		/** @overload */
		ArrayView<const T> exceptSuffix(std::size_t size) const {
			return ArrayView<const T>(*this).exceptSuffix(size);
		}

		/**
		 * @brief Release data storage
		 *
		 * Returns the data pointer and resets data pointer, size and deleter
		 * to be equivalent to a default-constructed instance. Deleting the
		 * returned array is user responsibility --- note the array might have
		 * a custom @ref deleter() and so @cpp delete[] @ce might not be always
		 * appropriate.
		 */
		T* release();

	private:
		T* _data;
		std::size_t _size;
		D _deleter;
	};

	/**
		@brief Construct a list-initialized array

		Convenience shortcut to the @ref Array::Array(InPlaceInitT, ArrayView<const T>)
		constructor. Not present as an implicit constructor in order to avoid the same
		usability issues as with @ref std::vector.
	*/
	template<class T> inline Array<T> array(ArrayView<const T> list) {
		return Array<T>{InPlaceInit, list};
	}

	/**
		@brief Construct a list-initialized array

		Convenience shortcut to the @ref Array::Array(InPlaceInitT, std::initializer_list<T>)
		constructor. Not present as an implicit constructor in order to avoid the same
		usability issues as with @ref std::vector.
	*/
	template<class T> inline Array<T> array(std::initializer_list<T> list) {
		return Array<T>{InPlaceInit, list};
	}

	/**
		@brief Make a view on an @ref Array

		Convenience alternative to converting to an @ref ArrayView explicitly.
	*/
	template<class T, class D> inline ArrayView<T> arrayView(Array<T, D>& array) {
		return ArrayView<T>{array};
	}

	/**
		@brief Make a view on a const @ref Array

		Convenience alternative to converting to an @ref ArrayView explicitly.
	*/
	template<class T, class D> inline ArrayView<const T> arrayView(const Array<T, D>& array) {
		return ArrayView<const T>{array};
	}

	/** @brief Reinterpret-cast an array */
	template<class U, class T, class D> inline ArrayView<U> arrayCast(Array<T, D>& array) {
		return arrayCast<U>(arrayView(array));
	}

	/** @overload */
	template<class U, class T, class D> inline ArrayView<const U> arrayCast(const Array<T, D>& array) {
		return arrayCast<const U>(arrayView(array));
	}

	/** @brief Array size */
	template<class T> std::size_t arraySize(const Array<T>& view) {
		return view.size();
	}

	template<class T, class D> inline Array<T, D>::Array(Array<T, D>&& other) noexcept : _data{other._data}, _size{other._size}, _deleter{other._deleter} {
		other._data = nullptr;
		other._size = 0;
		other._deleter = D{};
	}

	template<class T, class D> template<class ...Args> Array<T, D>::Array(DirectInitT, std::size_t size, Args&&... args) : Array{NoInit, size} {
		for (std::size_t i = 0; i != size; ++i)
			Implementation::construct(_data[i], std::forward<Args>(args)...);
	}

	template<class T, class D> Array<T, D>::Array(InPlaceInitT, const ArrayView<const T> list) : Array{NoInit, list.size()} {
		std::size_t i = 0;
		for (const T& item : list)
			// Can't use {}, see the GCC 4.8-specific overload for details
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ < 5
			Implementation::construct(_data[i++], item);
#else
			new(_data + i++) T{item};
#endif
	}

	template<class T, class D> Array<T, D>::Array(InPlaceInitT, std::initializer_list<T> list) : Array{InPlaceInit, arrayView(list)} { }

	template<class T, class D> inline Array<T, D>& Array<T, D>::operator=(Array<T, D>&& other) noexcept {
		using std::swap;
		swap(_data, other._data);
		swap(_size, other._size);
		swap(_deleter, other._deleter);
		return *this;
	}

	template<class T, class D> template<class U, class> const T& Array<T, D>::operator[](const U i) const {
		DEATH_DEBUG_ASSERT(std::size_t(i) < _size, ("Index %zu out of range for %zu elements", std::size_t(i), _size), _data[0]);
		return _data[i];
	}

	template<class T, class D> const T& Array<T, D>::front() const {
		DEATH_DEBUG_ASSERT(_size != 0, "Array is empty", _data[0]);
		return _data[0];
	}

	template<class T, class D> const T& Array<T, D>::back() const {
		DEATH_DEBUG_ASSERT(_size != 0, "Array is empty", _data[_size - 1]);
		return _data[_size - 1];
	}

	template<class T, class D> template<class U, class> T& Array<T, D>::operator[](const U i) {
		return const_cast<T&>(static_cast<const Array<T, D>&>(*this)[i]);
	}

	template<class T, class D> T& Array<T, D>::front() {
		return const_cast<T&>(static_cast<const Array<T, D>&>(*this).front());
	}

	template<class T, class D> T& Array<T, D>::back() {
		return const_cast<T&>(static_cast<const Array<T, D>&>(*this).back());
	}

	template<class T, class D> inline T* Array<T, D>::release() {
		T* const data = _data;
		_data = nullptr;
		_size = 0;
		_deleter = D{};
		return data;
	}

	namespace Implementation
	{
		template<class U, class T, class D> struct ArrayViewConverter<U, Array<T, D>> {
			template<class V = U, typename std::enable_if<std::is_convertible<T*, V*>::value, int>::type = 0> constexpr static ArrayView<U> from(Array<T, D>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return { &other[0], other.size() };
			}
			template<class V = U, typename std::enable_if<std::is_convertible<T*, V*>::value, int>::type = 0> constexpr static ArrayView<U> from(Array<T, D>&& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return { &other[0], other.size() };
			}
		};
		template<class U, class T, class D> struct ArrayViewConverter<const U, Array<T, D>> {
			template<class V = U, typename std::enable_if<std::is_convertible<T*, V*>::value, int>::type = 0> constexpr static ArrayView<const U> from(const Array<T, D>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return { &other[0], other.size() };
			}
		};
		template<class U, class T, class D> struct ArrayViewConverter<const U, Array<const T, D>> {
			template<class V = U, typename std::enable_if<std::is_convertible<T*, V*>::value, int>::type = 0> constexpr static ArrayView<const U> from(const Array<const T, D>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return { &other[0], other.size() };
			}
		};
		template<class T, class D> struct ErasedArrayViewConverter<Array<T, D>> : ArrayViewConverter<T, Array<T, D>> {};
		template<class T, class D> struct ErasedArrayViewConverter<const Array<T, D>> : ArrayViewConverter<const T, Array<T, D>> {};
	}
}}