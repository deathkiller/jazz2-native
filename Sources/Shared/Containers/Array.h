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
#include "ArrayView.h"
#include "Tags.h"

#include <initializer_list>
#include <new>
#include <type_traits>
#include <utility>

namespace Death::Containers
{
	template<class T, class = void(*)(T*, std::size_t)> class Array;
	template<std::size_t, class> class StaticArray;

	namespace Implementation
	{
		template<class T, class First, class ...Next> inline void construct(T& value, First&& first, Next&& ...next) {
			new(&value) T { std::forward<First>(first), std::forward<Next>(next)... };
		}
		template<class T> inline void construct(T& value) {
			new(&value) T();
		}

#if defined(DEATH_TARGET_GCC) && __GNUC__ < 5
		template<class T> inline void construct(T& value, T&& b) {
			new(&value) T(std::move(b));
		}
#endif

		template<class T> struct DefaultDeleter {
			T operator()() const {
				return T {};
			}
		};
		template<class T> struct DefaultDeleter<void(*)(T*, std::size_t)> {
			void(*operator()() const)(T*, std::size_t) { return nullptr; }
		};

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

		template<class T> T* noInitAllocate(std::size_t size, typename std::enable_if<std::is_trivial<T>::value>::type* = nullptr) {
			return new T[size];
		}
		template<class T> T* noInitAllocate(std::size_t size, typename std::enable_if<!std::is_trivial<T>::value>::type* = nullptr) {
			return reinterpret_cast<T*>(new char[size * sizeof(T)]);
		}

		template<class T> auto noInitDeleter(typename std::enable_if<std::is_trivial<T>::value>::type* = nullptr) -> void(*)(T*, std::size_t) {
			return nullptr;
		}
		template<class T> auto noInitDeleter(typename std::enable_if<!std::is_trivial<T>::value>::type* = nullptr) -> void(*)(T*, std::size_t) {
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
		typedef T Type;
		typedef D Deleter;

		template<class U, class V = typename std::enable_if<std::is_same<std::nullptr_t, U>::value>::type> /*implicit*/ Array(U) noexcept :
			_data{nullptr}, _size{0}, _deleter(Implementation::DefaultDeleter<D>{}()) {}

		/*implicit*/ Array() noexcept : _data(nullptr), _size(0), _deleter(Implementation::DefaultDeleter<D>{}()) {}

		explicit Array(DefaultInitT, std::size_t size) : _data{size ? new T[size] : nullptr}, _size{size}, _deleter{nullptr} {}

		explicit Array(ValueInitT, std::size_t size) : _data{size ? new T[size]() : nullptr}, _size{size}, _deleter{nullptr} {}

		explicit Array(NoInitT, std::size_t size) : _data{size ? Implementation::noInitAllocate<T>(size) : nullptr}, _size{size}, _deleter{Implementation::noInitDeleter<T>()} {}

		template<class... Args> explicit Array(DirectInitT, std::size_t size, Args&&... args);

		explicit Array(InPlaceInitT, std::initializer_list<T> list);

		explicit Array(std::size_t size) : Array{ValueInit, size} {}

		explicit Array(T* data, std::size_t size, D deleter = Implementation::DefaultDeleter<D>{}()) : _data{data}, _size{size}, _deleter(deleter) {}

		~Array() {
			Implementation::CallDeleter<T, D>{}(_deleter, _data, _size);
		}

		Array(const Array<T, D>&) = delete;

		Array(Array<T, D>&& other) noexcept;

		Array<T, D>& operator=(const Array<T, D>&) = delete;

		Array<T, D>& operator=(Array<T, D>&& other) noexcept;

		template<class U, class = decltype(Implementation::ArrayViewConverter<T, U>::to(std::declval<ArrayView<T>>()))> /*implicit*/ operator U() {
			return Implementation::ArrayViewConverter<T, U>::to(*this);
		}

		template<class U, class = decltype(Implementation::ArrayViewConverter<const T, U>::to(std::declval<ArrayView<const T>>()))> constexpr /*implicit*/ operator U() const {
			return Implementation::ArrayViewConverter<const T, U>::to(*this);
		}

#if !defined(DEATH_MSVC2019_COMPATIBILITY)
		explicit operator bool() const {
			return _data;
		}
#endif

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

		D deleter() const {
			return _deleter;
		}

		std::size_t size() const {
			return _size;
		}

		bool empty() const {
			return !_size;
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
			return _data + _size;
		}
		const T* end() const {
			return _data + _size;
		}
		const T* cend() const {
			return _data + _size;
		}

		T& front();
		const T& front() const;

		T& back();
		const T& back() const;

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

		template<std::size_t size> StaticArrayView<size, T> slice(T* begin) {
			return ArrayView<T>(*this).template slice<size>(begin);
		}
		template<std::size_t size> StaticArrayView<size, const T> slice(const T* begin) const {
			return ArrayView<const T>(*this).template slice<size>(begin);
		}
		template<std::size_t size> StaticArrayView<size, T> slice(std::size_t begin) {
			return ArrayView<T>(*this).template slice<size>(begin);
		}
		template<std::size_t size> StaticArrayView<size, const T> slice(std::size_t begin) const {
			return ArrayView<const T>(*this).template slice<size>(begin);
		}

		template<std::size_t begin_, std::size_t end_> StaticArrayView<end_ - begin_, T> slice() {
			return ArrayView<T>(*this).template slice<begin_, end_>();
		}

		template<std::size_t begin_, std::size_t end_> StaticArrayView<end_ - begin_, const T> slice() const {
			return ArrayView<const T>(*this).template slice<begin_, end_>();
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

		template<std::size_t viewSize> StaticArrayView<viewSize, T> prefix() {
			return ArrayView<T>(*this).template prefix<viewSize>();
		}
		template<std::size_t viewSize> StaticArrayView<viewSize, const T> prefix() const {
			return ArrayView<const T>(*this).template prefix<viewSize>();
		}

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

		ArrayView<T> except(std::size_t count) {
			return ArrayView<T>(*this).except(count);
		}
		ArrayView<const T> except(std::size_t count) const {
			return ArrayView<const T>(*this).except(count);
		}

		T* release();

	private:
		T* _data;
		std::size_t _size;
		D _deleter;
	};

	template<class T> inline Array<T> array(std::initializer_list<T> list) {
		return Array<T>{InPlaceInit, list};
	}

	template<class T, class D> inline ArrayView<T> arrayView(Array<T, D>& array) {
		return ArrayView<T>{array};
	}

	template<class T, class D> inline ArrayView<const T> arrayView(const Array<T, D>& array) {
		return ArrayView<const T>{array};
	}

	template<class U, class T, class D> inline ArrayView<U> arrayCast(Array<T, D>& array) {
		return arrayCast<U>(arrayView(array));
	}

	template<class U, class T, class D> inline ArrayView<const U> arrayCast(const Array<T, D>& array) {
		return arrayCast<const U>(arrayView(array));
	}

	template<class T> std::size_t arraySize(const Array<T>& view) {
		return view.size();
	}

	template<class T, class D> inline Array<T, D>::Array(Array<T, D>&& other) noexcept : _data{other._data}, _size{other._size}, _deleter{other._deleter} {
		other._data = nullptr;
		other._size = 0;
		other._deleter = D {};
	}

	template<class T, class D> template<class ...Args> Array<T, D>::Array(DirectInitT, std::size_t size, Args&&... args) : Array{NoInit, size} {
		for (std::size_t i = 0; i != size; ++i)
			Implementation::construct(_data[i], std::forward<Args>(args)...);
	}

	template<class T, class D> Array<T, D>::Array(InPlaceInitT, std::initializer_list<T> list) : Array{NoInit, list.size()} {
		std::size_t i = 0;
		for (const T& item : list) new(_data + i++) T { item };
	}

	template<class T, class D> inline Array<T, D>& Array<T, D>::operator=(Array<T, D>&& other) noexcept {
		using std::swap;
		swap(_data, other._data);
		swap(_size, other._size);
		swap(_deleter, other._deleter);
		return *this;
	}

	template<class T, class D> const T& Array<T, D>::front() const {
		DEATH_ASSERT(_size != 0, _data[0], "Containers::Array::front(): Array is empty");
		return _data[0];
	}

	template<class T, class D> const T& Array<T, D>::back() const {
		DEATH_ASSERT(_size != 0, _data[_size - 1], "Containers::Array::back(): Array is empty");
		return _data[_size - 1];
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
		_deleter = D {};
		return data;
	}

	namespace Implementation
	{
		template<class U, class T, class D> struct ArrayViewConverter<U, Array<T, D>> {
			template<class V = U> constexpr static typename std::enable_if<std::is_convertible<T*, V*>::value, ArrayView<U>>::type from(Array<T, D>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return { &other[0], other.size() };
			}
			template<class V = U> constexpr static typename std::enable_if<std::is_convertible<T*, V*>::value, ArrayView<U>>::type from(Array<T, D>&& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return { &other[0], other.size() };
			}
		};
		template<class U, class T, class D> struct ArrayViewConverter<const U, Array<T, D>> {
			template<class V = U> constexpr static typename std::enable_if<std::is_convertible<T*, V*>::value, ArrayView<const U>>::type from(const Array<T, D>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return { &other[0], other.size() };
			}
		};
		template<class U, class T, class D> struct ArrayViewConverter<const U, Array<const T, D>> {
			template<class V = U> constexpr static typename std::enable_if<std::is_convertible<T*, V*>::value, ArrayView<const U>>::type from(const Array<const T, D>& other) {
				static_assert(sizeof(T) == sizeof(U), "Types are not compatible");
				return { &other[0], other.size() };
			}
		};
		template<class T, class D> struct ErasedArrayViewConverter<Array<T, D>> : ArrayViewConverter<T, Array<T, D>> {};
		template<class T, class D> struct ErasedArrayViewConverter<const Array<T, D>> : ArrayViewConverter<const T, Array<T, D>> {};
	}
}