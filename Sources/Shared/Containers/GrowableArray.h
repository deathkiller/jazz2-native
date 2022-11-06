// Copyright © 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016,
//             2017, 2018, 2019, 2020, 2021, 2022
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

#include <cstdlib>
#include <cstring>

#include "Array.h"

namespace Death::Containers
{
	namespace Implementation
	{
		enum : std::size_t {
			DefaultAllocationAlignment =
			// Emscripten has __STDCPP_DEFAULT_NEW_ALIGNMENT__ set to 16 but actually does just 8, so don't use this macro there:
			// https://github.com/emscripten-core/emscripten/issues/10072
#if defined(__STDCPP_DEFAULT_NEW_ALIGNMENT__) && !defined(DEATH_TARGET_EMSCRIPTEN)
			__STDCPP_DEFAULT_NEW_ALIGNMENT__
#else
			2 * sizeof(std::size_t)
#endif
		};

		template<class T> inline void arrayConstruct(DefaultInitT, T*, T*, typename std::enable_if<
		std::is_trivially_constructible<T>::value
		>::type* = nullptr) {
			// Nothing to do
		}

		template<class T> inline void arrayConstruct(DefaultInitT, T* begin, T* const end, typename std::enable_if<!
			std::is_trivially_constructible<T>::value
		>::type* = nullptr) {
			// Needs to be < because sometimes begin > end. No {}, we want trivial types non-initialized
			for (; begin < end; ++begin) new(begin) T;
		}

		template<class T> inline void arrayConstruct(ValueInitT, T* const begin, T* const end, typename std::enable_if<
			std::is_trivially_constructible<T>::value
		>::type* = nullptr) {
			if (begin < end) std::memset(begin, 0, (end - begin) * sizeof(T));
		}

		template<class T> inline void arrayConstruct(ValueInitT, T* begin, T* const end, typename std::enable_if<!
			std::is_trivially_constructible<T>::value
		>::type* = nullptr) {
			// Needs to be < because sometimes begin > end
			for (; begin < end; ++begin) new(begin) T {};
		}

		template<class T> struct AllocatorTraits {
			enum : std::size_t {
				Offset = alignof(T) < sizeof(std::size_t) ? sizeof(std::size_t) :
				(alignof(T) < Implementation::DefaultAllocationAlignment ?
					alignof(T) : Implementation::DefaultAllocationAlignment)
			};
		};

	}

	 /**
		 @brief New-based allocator for growable arrays

		 An @ref ArrayAllocator that allocates and deallocates memory using the C++
		 @cpp new[] @ce / @cpp delete[] @ce constructs, reserving an extra space
		 * *before* to store array capacity.

		 All reallocation operations expect that @p T is nothrow move-constructible.
	 */
	template<class T> struct ArrayNewAllocator
	{
		typedef T Type;

		enum : std::size_t {
			AllocationOffset = Implementation::AllocatorTraits<T>::Offset
		};

		/**
		 * @brief Allocate (but not construct) an array of given capacity
		 *
		 * @cpp new[] @ce-allocates a @cpp char @ce array with an extra space to
		 * store @p capacity *before* the front, returning it cast to @cpp T* @ce.
		 * The allocation is guaranteed to follow `T` allocation requirements up to
		 * the platform default allocation alignment.
		 */
		static T* allocate(std::size_t capacity) {
			char* const memory = new char[capacity * sizeof(T) + AllocationOffset];
			reinterpret_cast<std::size_t*>(memory)[0] = capacity;
			return reinterpret_cast<T*>(memory + AllocationOffset);
		}

		/**
		 * @brief Reallocate an array to given capacity
		 *
		 * Calls @p allocate(), move-constructs @p prevSize elements from @p array
		 * into the new array, calls destructors on the original elements, calls
		 * @ref deallocate() and updates the @p array reference to point to the new
		 * array. The allocation is guaranteed to follow `T` allocation
		 * requirements up to the platform default allocation alignment.
		 */
		static void reallocate(T*& array, std::size_t prevSize, std::size_t newCapacity);

		/**
		 * @brief Deallocate an array
		 *
		 * Calls @cpp delete[] @ce on a pointer offset by the extra space needed to
		 * store its capacity.
		 */
		static void deallocate(T* data) {
			delete[](reinterpret_cast<char*>(data) - AllocationOffset);
		}

		/**
		 * @brief Grow an array
		 *
		 * If current occupied size (including the space needed to store capacity)
		 * is less than 64 bytes, the capacity always doubled, with the allocation
		 * being at least as large as @cpp __STDCPP_DEFAULT_NEW_ALIGNMENT__ @ce
		 * (*usually* @cpp 2*sizeof(std::size_t) @ce). After that, the capacity is
		 * increased to 1.5x of current capacity (again including the space needed
		 * to store capacity). This is similar to what MSVC STL does with
		 * @ref std::vector, except for libc++ / libstdc++, which both use a factor
		 * of 2. With a factor of 2 the allocation would crawl forward in memory,
		 * never able to reuse the holes after previous allocations, with a factor
		 * 1.5 it's possible after four reallocations. Further info in
		 * [Folly FBVector docs](https://github.com/facebook/folly/blob/master/folly/docs/FBVector.md#memory-handling).
		 */
		static std::size_t grow(T* array, std::size_t desired);

		/**
		 * @brief Array capacity
		 *
		 * Retrieves the capacity that's stored *before* the front of the @p array.
		 */
		static std::size_t capacity(T* array) {
			return *reinterpret_cast<std::size_t*>(reinterpret_cast<char*>(array) - AllocationOffset);
		}

		/**
		 * @brief Array base address
		 *
		 * Returns the address with @ref AllocationOffset subtracted.
		 */
		static void* base(T* array) {
			return reinterpret_cast<char*>(array) - AllocationOffset;
		}

		/**
		 * @brief Array deleter
		 *
		 * Calls a destructor on @p size elements and then delegates into
		 * @ref deallocate().
		 */
		static void deleter(T* data, std::size_t size) {
			for (T* it = data, *end = data + size; it != end; ++it) it->~T();
			deallocate(data);
		}
	};

	/**
		@brief Malloc-based allocator for growable arrays

		An @ref ArrayAllocator that allocates and deallocates memory using the C
		@ref std::malloc() / @ref std::free() constructs in order to be able to use
		@ref std::realloc() for fast reallocations. Similarly to @ref ArrayNewAllocator
		it's reserving an extra space *before* to store array capacity.

		All reallocation operations expect that @p T is trivially copyable. If it's
		not, use @ref ArrayNewAllocator instead.

		Compared to @ref ArrayNewAllocator, this allocator stores array capacity in
		bytes and, together with the fact that @ref std::free() doesn't care about the
		actual array type, growable arrays using this allocator can be freely cast to
		different compatible types using @ref arrayAllocatorCast().
	*/
	template<class T> struct ArrayMallocAllocator {
		static_assert(std::is_trivially_copyable<T>::value, "only trivially copyable types are usable with this allocator");

		typedef T Type; /**< Pointer type */

		enum : std::size_t {
			/**
			 * Offset at the beginning of the allocation to store allocation
			 * capacity. At least as large as @ref std::size_t. If the type
			 * alignment is larger than that (for example @cpp double @ce on a
			 * 32-bit platform), then it's equal to type alignment, but only at
			 * most as large as the default allocation alignment.
			 */
			AllocationOffset = Implementation::AllocatorTraits<T>::Offset
		};

		/**
		 * @brief Allocate an array of given capacity
		 *
		 * @ref std::malloc()'s an @cpp char @ce array with an extra space to store
		 * @p capacity *before* the front, returning it cast to @cpp T* @ce. The
		 * allocation is guaranteed to follow `T` allocation requirements up to the
		 * platform default allocation alignment.
		 */
		static T* allocate(std::size_t capacity) {
			/* Compared to ArrayNewAllocator, here the capacity is stored in bytes
			   so it's possible to "reinterpret" the array into a different type
			   (as the deleter is a typeless std::free() in any case) */
			const std::size_t inBytes = capacity * sizeof(T) + AllocationOffset;
			char* const memory = static_cast<char*>(std::malloc(inBytes));
			reinterpret_cast<std::size_t*>(memory)[0] = inBytes;
			return reinterpret_cast<T*>(memory + AllocationOffset);
		}

		/**
		 * @brief Reallocate an array to given capacity
		 *
		 * Calls @ref std::realloc() on @p array (offset by the space to store
		 * capacity) and then updates the stored capacity to @p newCapacity and the
		 * @p array reference to point to the new (offset) location, in case the
		 * reallocation wasn't done in-place. The @p prevSize parameter is ignored,
		 * as @ref std::realloc() always copies the whole original capacity. The
		 * allocation is guaranteed to follow `T` allocation requirements up to the
		 * platform default allocation alignment.
		 */
		static void reallocate(T*& array, std::size_t prevSize, std::size_t newCapacity);

		/**
		 * @brief Deallocate an array
		 *
		 * Calls @ref std::free() on a pointer offset by the extra space needed to
		 * store its capacity.
		 */
		static void deallocate(T* data) {
			if (data) std::free(reinterpret_cast<char*>(data) - AllocationOffset);
		}

		/**
		 * @brief Grow an array
		 *
		 * Behaves the same as @ref ArrayNewAllocator::grow().
		 */
		static std::size_t grow(T* array, std::size_t desired);

		/**
		 * @brief Array capacity
		 *
		 * Retrieves the capacity that's stored *before* the front of the @p array.
		 */
		static std::size_t capacity(T* array) {
			return (*reinterpret_cast<std::size_t*>(reinterpret_cast<char*>(array) - AllocationOffset) - AllocationOffset) / sizeof(T);
		}

		/**
		 * @brief Array base address
		 *
		 * Returns the address with @ref AllocationOffset subtracted.
		 */
		static void* base(T* array) {
			return reinterpret_cast<char*>(array) - AllocationOffset;
		}

		/**
		 * @brief Array deleter
		 *
		 * Since the types have trivial destructors, directly delegates into
		 * @ref deallocate(). The @p size parameter is unused.
		 */
		static void deleter(T* data, std::size_t size) {
			static_cast<void>(size);
			deallocate(data);
		}
	};

	/**
		@brief Allocator for growable arrays

		Is either @ref ArrayMallocAllocator for trivially copyable @p T, or
		@ref ArrayNewAllocator otherwise, in which case reallocation operations expect
		@p T to be nothrow move-constructible.

		See @ref Containers-Array-growable for an introduction to growable arrays.

		You can provide your own allocator by implementing a class that with @ref Type,
		@ref allocate(), @ref reallocate(), @ref deallocate(), @ref grow(),
		@ref capacity(), @ref base() and @ref deleter() following the documented
		semantics.
	*/
	template<class T> using ArrayAllocator = typename std::conditional<std::is_trivially_copyable<T>::value , ArrayMallocAllocator<T>, ArrayNewAllocator<T>>::type;

	/**
		@brief Reinterpret-cast a growable array

		If the array is growable using @ref ArrayMallocAllocator (which is aliased to
		@ref ArrayAllocator for all trivially-copyable types), the deleter is a simple
		call to a typeless @ref std::free(). This makes it possible to change the array
		type without having to use a different deleter, losing the growable property in
		the process.

		Equivalently to to @ref arrayCast(), the size of the new array is calculated as
		@cpp view.size()*sizeof(T)/sizeof(U) @ce. Expects that both types are
		trivially copyable and [standard layout](http://en.cppreference.com/w/cpp/concept/StandardLayoutType)
		and the total byte size doesn't change.
	*/
	template<class U, class T> Array<U> arrayAllocatorCast(Array<T>&& array);

	template<class U, template<class> class Allocator, class T> Array<U> arrayAllocatorCast(Array<T>&& array) {
		static_assert(std::is_standard_layout<T>::value, "the source type is not standard layout");
		static_assert(std::is_standard_layout<U>::value, "the target type is not standard layout");
		static_assert(std::is_trivially_copyable<T>::value && std::is_trivially_copyable<U>::value, "only trivially copyable types can use the allocator cast");
		/* Unlike arrayInsert() etc, this is not called that often and should be as
		   checked as possible, so it's not a debug assert */
		DEATH_ASSERT(array.data() == nullptr ||
			(array.deleter() == Allocator<T>::deleter && std::is_base_of<ArrayMallocAllocator<T>, Allocator<T>>::value),
			"Containers::arrayAllocatorCast(): the array has to use the ArrayMallocAllocator or a derivative", {});
		const std::size_t size = array.size() * sizeof(T) / sizeof(U);
		DEATH_ASSERT(size * sizeof(U) == array.size() * sizeof(T),
			"Containers::arrayAllocatorCast(): can't reinterpret" << array.size() << sizeof(T) << Utility::Debug::nospace << "-byte items into a" << sizeof(U) << Utility::Debug::nospace << "-byte type", {});
		return Array<U>{reinterpret_cast<U*>(array.release()), size, Allocator<U>::deleter};
	}

	template<class U, class T> Array<U> arrayAllocatorCast(Array<T>&& array) {
		return arrayAllocatorCast<U, ArrayAllocator, T>(std::move(array));
	}

	/**
		@brief Whether an array is growable

		Returns @cpp true @ce if the array is growable and using given @p Allocator,
		@cpp false @ce otherwise. Note that even non-growable arrays are usable with
		the @ref arrayAppend(), @ref arrayReserve(), ... family of utilities --- these
		will reallocate the array using provided allocator if needed.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> bool arrayIsGrowable(Array<T>& array);

	template<template<class T> class Allocator, class T> inline bool arrayIsGrowable(Array<T>& array) {
		return arrayIsGrowable<T, Allocator<T>>(array);
	}

	template<class T, class Allocator = ArrayAllocator<T>> std::size_t arrayCapacity(Array<T>& array);

	template<template<class T> class Allocator, class T> inline std::size_t arrayCapacity(Array<T>& array) {
		return arrayCapacity<T, Allocator<T>>(array);
	}

	template<class T, class Allocator = ArrayAllocator<T>> std::size_t arrayReserve(Array<T>& array, std::size_t capacity);

	template<template<class T> class Allocator, class T> inline std::size_t arrayReserve(Array<T>& array, std::size_t capacity) {
		return arrayReserve<T, Allocator<T>>(array, capacity);
	}

	template<class T, class Allocator = ArrayAllocator<T>> void arrayResize(Array<T>& array, DefaultInitT, std::size_t size);

	template<template<class> class Allocator, class T> inline void arrayResize(Array<T>& array, DefaultInitT, std::size_t size) {
		arrayResize<T, Allocator<T>>(array, DefaultInit, size);
	}

	template<class T, class Allocator = ArrayAllocator<T>> void arrayResize(Array<T>& array, ValueInitT, std::size_t size);

	template<template<class> class Allocator, class T> inline void arrayResize(Array<T>& array, ValueInitT, std::size_t size) {
		arrayResize<T, Allocator<T>>(array, ValueInit, size);
	}

	template<class T, class Allocator = ArrayAllocator<T>> inline void arrayResize(Array<T>& array, std::size_t size) {
		return arrayResize<T, Allocator>(array, ValueInit, size);
	}

	template<template<class> class Allocator, class T> inline void arrayResize(Array<T>& array, std::size_t size) {
		arrayResize<T, Allocator<T>>(array, size);
	}

	template<class T, class Allocator = ArrayAllocator<T>> void arrayResize(Array<T>& array, NoInitT, std::size_t size);

	template<template<class> class Allocator, class T> inline void arrayResize(Array<T>& array, NoInitT, std::size_t size) {
		arrayResize<T, Allocator<T>>(array, NoInit, size);
	}

	template<class T, class ...Args> void arrayResize(Array<T>& array, DirectInitT, std::size_t size, Args&&... args);

	template<class T, class Allocator, class ...Args> void arrayResize(Array<T>& array, DirectInitT, std::size_t size, Args&&... args);

	template<template<class> class Allocator, class T, class ...Args> inline void arrayResize(Array<T>& array, DirectInitT, std::size_t size, Args&&... args) {
		arrayResize<T, Allocator<T>>(array, DirectInit, size, std::forward<Args>(args)...);
	}

	template<class T, class Allocator = ArrayAllocator<T>> inline void arrayResize(Array<T>& array, std::size_t size, const typename std::common_type<T>::type& value) {
		arrayResize<T, Allocator>(array, DirectInit, size, value);
	}

	template<template<class> class Allocator, class T> inline void arrayResize(Array<T>& array, std::size_t size, const typename std::common_type<T>::type& value) {
		arrayResize<T, Allocator<T>>(array, size, value);
	}

	template<class T, class Allocator = ArrayAllocator<T>> T& arrayAppend(Array<T>& array, const typename std::common_type<T>::type& value);

	template<template<class> class Allocator, class T> inline T& arrayAppend(Array<T>& array, const typename std::common_type<T>::type& value) {
		return arrayAppend<T, Allocator<T>>(array, value);
	}

	template<class T, class ...Args> T& arrayAppend(Array<T>& array, InPlaceInitT, Args&&... args);

	template<class T, class Allocator, class ...Args> T& arrayAppend(Array<T>& array, InPlaceInitT, Args&&... args);

	template<template<class> class Allocator, class T, class ...Args> inline T& arrayAppend(Array<T>& array, InPlaceInitT, Args&&... args) {
		return arrayAppend<T, Allocator<T>>(array, InPlaceInit, std::forward<Args>(args)...);
	}

	template<class T, class Allocator = ArrayAllocator<T>> inline T& arrayAppend(Array<T>& array, typename std::common_type<T>::type&& value) {
		return arrayAppend<T, Allocator>(array, InPlaceInit, std::move(value));
	}

	template<template<class> class Allocator, class T> inline T& arrayAppend(Array<T>& array, typename std::common_type<T>::type&& value) {
		return arrayAppend<T, Allocator<T>>(array, InPlaceInit, std::move(value));
	}

	template<class T, class Allocator = ArrayAllocator<T>> ArrayView<T> arrayAppend(Array<T>& array, ArrayView<const T> values);

	template<template<class> class Allocator, class T> inline ArrayView<T> arrayAppend(Array<T>& array, ArrayView<const T> values) {
		return arrayAppend<T, Allocator<T>>(array, values);
	}

	template<class T, class Allocator = ArrayAllocator<T>> inline ArrayView<T>  arrayAppend(Array<T>& array, std::initializer_list<T> values) {
		return arrayAppend<T, Allocator>(array, arrayView(values));
	}

	template<template<class> class Allocator, class T> inline ArrayView<T>  arrayAppend(Array<T>& array, std::initializer_list<T> values) {
		return arrayAppend<T, Allocator<T>>(array, values);
	}

	template<class T, class Allocator = ArrayAllocator<T>> ArrayView<T> arrayAppend(Array<T>& array, NoInitT, std::size_t count);

	template<template<class> class Allocator, class T> inline ArrayView<T> arrayAppend(Array<T>& array, NoInitT, std::size_t count) {
		return arrayAppend<T, Allocator<T>>(array, NoInit, count);
	}

	template<class T, class Allocator = ArrayAllocator<T>> T& arrayInsert(Array<T>& array, std::size_t index, const typename std::common_type<T>::type& value);

	template<template<class> class Allocator, class T> T& arrayInsert(Array<T>& array, std::size_t index, const typename std::common_type<T>::type& value) {
		return arrayInsert<T, Allocator<T>>(array, index, value);
	}

	template<class T, class ...Args> T& arrayInsert(Array<T>& array, std::size_t index, InPlaceInitT, Args&&... args);

	template<class T, class Allocator, class ...Args> T& arrayInsert(Array<T>& array, std::size_t index, InPlaceInitT, Args&&... args);

	template<template<class> class Allocator, class T, class ...Args> T& arrayInsert(Array<T>& array, std::size_t index, InPlaceInitT, Args&&... args) {
		return arrayInsert<T, Allocator<T>>(array, index, std::forward<Args>(args)...);
	}

	template<class T, class Allocator = ArrayAllocator<T>> inline T& arrayInsert(Array<T>& array, std::size_t index, typename std::common_type<T>::type&& value) {
		return arrayInsert<T, Allocator>(array, index, InPlaceInit, std::move(value));
	}

	template<template<class> class Allocator, class T> inline T& arrayInsert(Array<T>& array, std::size_t index, typename std::common_type<T>::type&& value) {
		return arrayInsert<T, Allocator<T>>(array, index, InPlaceInit, std::move(value));
	}

	template<class T, class Allocator = ArrayAllocator<T>> ArrayView<T> arrayInsert(Array<T>& array, std::size_t index, ArrayView<const T> values);

	template<template<class> class Allocator, class T> inline ArrayView<T> arrayInsert(Array<T>& array, std::size_t index, ArrayView<const T> values) {
		return arrayInsert<T, Allocator<T>>(array, index, values);
	}

	template<class T, class Allocator = ArrayAllocator<T>> ArrayView<T>  arrayInsert(Array<T>& array, std::size_t index, std::initializer_list<T> values) {
		return arrayInsert<T, Allocator>(array, index, arrayView(values));
	}

	template<template<class> class Allocator, class T> inline ArrayView<T>  arrayInsert(Array<T>& array, std::size_t index, std::initializer_list<T> values) {
		return arrayInsert<T, Allocator<T>>(array, index, values);
	}

	template<class T, class Allocator = ArrayAllocator<T>> ArrayView<T> arrayInsert(Array<T>& array, std::size_t index, NoInitT, std::size_t count);

	template<template<class> class Allocator, class T> inline ArrayView<T> arrayInsert(Array<T>& array, std::size_t index, NoInitT, std::size_t count) {
		return arrayInsert<T, Allocator<T>>(array, index, NoInit, count);
	}

	template<class T, class Allocator = ArrayAllocator<T>> void arrayRemove(Array<T>& array, std::size_t index, std::size_t count = 1);

	template<template<class> class Allocator, class T> inline void arrayRemove(Array<T>& array, std::size_t index, std::size_t count = 1) {
		arrayRemove<T, Allocator<T>>(array, index, count);
	}

	template<class T, class Allocator = ArrayAllocator<T>> void arrayRemoveUnordered(Array<T>& array, std::size_t index, std::size_t count = 1);

	template<template<class> class Allocator, class T> inline void arrayRemoveUnordered(Array<T>& array, std::size_t index, std::size_t count = 1) {
		arrayRemoveUnordered<T, Allocator<T>>(array, index, count);
	}

	template<class T, class Allocator = ArrayAllocator<T>> void arrayRemoveSuffix(Array<T>& array, std::size_t count = 1);

	template<template<class> class Allocator, class T> inline void arrayRemoveSuffix(Array<T>& array, std::size_t count = 1) {
		arrayRemoveSuffix<T, Allocator<T>>(array, count);
	}

	template<class T, class Allocator = ArrayAllocator<T>> void arrayShrink(Array<T>& array, NoInitT = NoInit);

	template<class T, class Allocator = ArrayAllocator<T>> void arrayShrink(Array<T>& array, DefaultInitT);

	template<template<class> class Allocator, class T> inline void arrayShrink(Array<T>& array) {
		arrayShrink<T, Allocator<T>>(array);
	}

	namespace Implementation
	{
		// Used to avoid calling getter functions to speed up debug builds
		template<class T> struct ArrayGuts {
			T* data;
			std::size_t size;
			void(*deleter)(T*, std::size_t);
		};

		template<class T> inline void arrayMoveConstruct(T* const src, T* const dst, const std::size_t count, typename std::enable_if<
			std::is_trivially_copyable<T>::value
		>::type* = nullptr) {
			// Apparently memcpy() can't be called with null pointers, even if size is zero. I call that bullying.
			if (count) std::memcpy(dst, src, count * sizeof(T));
		}

		template<class T> inline void arrayMoveConstruct(T* src, T* dst, const std::size_t count, typename std::enable_if<!
			std::is_trivially_copyable<T>::value
		>::type* = nullptr) {
			static_assert(std::is_nothrow_move_constructible<T>::value,
				"nothrow move-constructible type is required");
			for (T* end = src + count; src != end; ++src, ++dst)
				// Can't use {}, see the GCC 4.8-specific overload for details
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ < 5
				Implementation::construct(*dst, std::move(*src));
#else
				new(dst) T { std::move(*src) };
#endif
		}

		template<class T> inline void arrayMoveAssign(T* const src, T* const dst, const std::size_t count, typename std::enable_if<
			std::is_trivially_copyable<T>::value
		>::type* = nullptr) {
			// Apparently memcpy() can't be called with null pointers, even if size is zero. I call that bullying.
			if (count) std::memcpy(dst, src, count * sizeof(T));
		}

		template<class T> inline void arrayMoveAssign(T* src, T* dst, const std::size_t count, typename std::enable_if<!
			std::is_trivially_copyable<T>::value
		>::type* = nullptr) {
			static_assert(std::is_nothrow_move_assignable<T>::value,
				"nothrow move-assignable type is required");
			for (T* end = src + count; src != end; ++src, ++dst)
				*dst = std::move(*src);
		}

		template<class T> inline void arrayCopyConstruct(const T* const src, T* const dst, const std::size_t count, typename std::enable_if<
			std::is_trivially_copyable<T>::value
		>::type* = nullptr) {
			// Apparently memcpy() can't be called with null pointers, even if size is zero. I call that bullying.
			if (count) std::memcpy(dst, src, count * sizeof(T));
		}

		template<class T> inline void arrayCopyConstruct(const T* src, T* dst, const std::size_t count, typename std::enable_if<!
			std::is_trivially_copyable<T>::value
		>::type* = nullptr) {
			for (const T* end = src + count; src != end; ++src, ++dst)
				// Can't use {}, see the GCC 4.8-specific overload for details
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) &&  __GNUC__ < 5
				Implementation::construct(*dst, *src);
#else
				new(dst) T { *src };
#endif
		}

		template<class T> inline void arrayDestruct(T*, T*, typename std::enable_if<std::is_trivially_destructible<T>::value>::type* = nullptr) {
			// Nothing to do
		}

		template<class T> inline void arrayDestruct(T* begin, T* const end, typename std::enable_if<!std::is_trivially_destructible<T>::value>::type* = nullptr) {
			// Needs to be < because sometimes begin > end
			for (; begin < end; ++begin) begin->~T();
		}

		template<class T> inline std::size_t arrayGrowth(const std::size_t currentCapacity, const std::size_t desiredCapacity) {
			const std::size_t currentCapacityInBytes = sizeof(T) * currentCapacity + Implementation::AllocatorTraits<T>::Offset;

			// For small allocations we want to tightly fit into size buckets (8, 16, 32, 64 bytes), so it's better to double
			// the capacity every time. For larger, increase just by 50%. The capacity is calculated including the space needed
			// to store the capacity value (so e.g. a 16-byte allocation can store two ints, but when it's doubled to 32 bytes,
			// it can store six of them).
			std::size_t grown;
			if (currentCapacityInBytes < DefaultAllocationAlignment)
				grown = DefaultAllocationAlignment;
			else if (currentCapacityInBytes < 64)
				grown = currentCapacityInBytes * 2;
			else
				grown = currentCapacityInBytes + currentCapacityInBytes / 2;

			const std::size_t candidate = (grown - Implementation::AllocatorTraits<T>::Offset) / sizeof(T);
			return desiredCapacity > candidate ? desiredCapacity : candidate;
		}

	}

	template<class T> void ArrayNewAllocator<T>::reallocate(T*& array, const std::size_t prevSize, const std::size_t newCapacity) {
		T* newArray = allocate(newCapacity);
		static_assert(std::is_nothrow_move_constructible<T>::value, "nothrow move-constructible type is required");
		for (T* src = array, *end = src + prevSize, *dst = newArray; src != end; ++src, ++dst)
			// Can't use {}, see the GCC 4.8-specific overload for details
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ < 5
			Implementation::construct(*dst, std::move(*src));
#else
			new(dst) T { std::move(*src) };
#endif
		for (T* it = array, *end = array + prevSize; it < end; ++it) it->~T();
		deallocate(array);
		array = newArray;
	}

	template<class T> void ArrayMallocAllocator<T>::reallocate(T*& array, std::size_t, const std::size_t newCapacity) {
		const std::size_t inBytes = newCapacity * sizeof(T) + AllocationOffset;
		char* const memory = static_cast<char*>(std::realloc(reinterpret_cast<char*>(array) - AllocationOffset, inBytes));
		reinterpret_cast<std::size_t*>(memory)[0] = inBytes;
		array = reinterpret_cast<T*>(memory + AllocationOffset);
	}

	template<class T> std::size_t ArrayNewAllocator<T>::grow(T* const array, const std::size_t desiredCapacity) {
		return Implementation::arrayGrowth<T>(array ? capacity(array) : 0, desiredCapacity);
	}

	template<class T> std::size_t ArrayMallocAllocator<T>::grow(T* const array, const std::size_t desiredCapacity) {
		return Implementation::arrayGrowth<T>(array ? capacity(array) : 0, desiredCapacity);
	}

	template<class T, class Allocator> bool arrayIsGrowable(Array<T>& array) {
		return array.deleter() == Allocator::deleter;
	}

	template<class T, class Allocator> std::size_t arrayCapacity(Array<T>& array) {
		if (array.deleter() == Allocator::deleter)
			return Allocator::capacity(array.data());
		return array.size();
	}

	template<class T, class Allocator> std::size_t arrayReserve(Array<T>& array, const std::size_t capacity) {
		// Direct access & value caching to speed up debug builds
		auto& arrayGuts = reinterpret_cast<Implementation::ArrayGuts<T>&>(array);
		const bool hasGrowingDeleter = arrayGuts.deleter == Allocator::deleter;

		// If the capacity is large enough, nothing to do (even if we have the array allocated by something different)
		const std::size_t currentCapacity = arrayCapacity<T, Allocator>(array);
		if (currentCapacity >= capacity) return currentCapacity;

		// Otherwise allocate a new array, move the previous data there and replace the old Array instance with it. Array's deleter
		// will take care of destructing & deallocating the previous memory.
		if (!hasGrowingDeleter) {
			T* newArray = Allocator::allocate(capacity);
			Implementation::arrayMoveConstruct<T>(arrayGuts.data, newArray, arrayGuts.size);
			array = Array<T> { newArray, arrayGuts.size, Allocator::deleter };
		} else Allocator::reallocate(arrayGuts.data, arrayGuts.size, capacity);

		return capacity;
	}

	template<class T, class Allocator> void arrayResize(Array<T>& array, NoInitT, const std::size_t size) {
		// Direct access & value caching to speed up debug builds
		auto& arrayGuts = reinterpret_cast<Implementation::ArrayGuts<T>&>(array);
		const bool hasGrowingDeleter = arrayGuts.deleter == Allocator::deleter;

		// New size is the same as the old one, nothing to do
		if (arrayGuts.size == size) return;

		// Reallocate if we don't have our growable deleter, as the default deleter  might then call destructors even in the non-initialized area ...
		if (!hasGrowingDeleter) {
			T* newArray = Allocator::allocate(size);
			Implementation::arrayMoveConstruct<T>(array, newArray,
				// Move the min of the two sizes -- if we shrink, move only what will fit in the new array; if we extend,
				// move only what's initialized in the original and left the rest not initialized
				arrayGuts.size < size ? arrayGuts.size : size);
			array = Array<T> { newArray, size, Allocator::deleter };

			// ... or the desired size is larger than the capacity. In that case make use of the reallocate() function that might be able to grow in-place.
		} else if (Allocator::capacity(array) < size) {
			Allocator::reallocate(arrayGuts.data,
				// Move the min of the two sizes -- if we shrink, move only what will fit in the new array; if we extend,
				// move only what's initialized in the original and left the rest not initialized
				arrayGuts.size < size ? arrayGuts.size : size, size);
			arrayGuts.size = size;

			// Otherwise call a destructor on the extra elements. If we get here, we have our growable deleter and didn't
			// need to reallocate (which would make this unnecessary).
		} else {
			Implementation::arrayDestruct<T>(arrayGuts.data + size, arrayGuts.data + arrayGuts.size);
			// This is a NoInit resize, so not constructing the new elements, only updating the size
			arrayGuts.size = size;
		}
	}

	template<class T, class Allocator> void arrayResize(Array<T>& array, DefaultInitT, const std::size_t size) {
		const std::size_t prevSize = array.size();
		arrayResize<T, Allocator>(array, NoInit, size);
		Implementation::arrayConstruct(DefaultInit, array + prevSize, array.end());
	}

	template<class T, class Allocator> void arrayResize(Array<T>& array, ValueInitT, const std::size_t size) {
		const std::size_t prevSize = array.size();
		arrayResize<T, Allocator>(array, NoInit, size);
		Implementation::arrayConstruct(ValueInit, array + prevSize, array.end());
	}

	template<class T, class Allocator, class ...Args> void arrayResize(Array<T>& array, DirectInitT, const std::size_t size, Args&&... args) {
		const std::size_t prevSize = array.size();
		arrayResize<T, Allocator>(array, NoInit, size);

		// In-place construct the new elements. No helper function for this as there's no way we could memcpy such a thing.
		for (T* it = array + prevSize; it < array.end(); ++it)
			Implementation::construct(*it, std::forward<Args>(args)...);
	}

	template<class T, class ...Args> inline void arrayResize(Array<T>& array, DirectInitT, const std::size_t size, Args&&... args) {
		arrayResize<T, ArrayAllocator<T>, Args...>(array, DirectInit, size, std::forward<Args>(args)...);
	}

	namespace Implementation {

		template<class T, class Allocator> T* arrayGrowBy(Array<T>& array, const std::size_t count) {
			// Direct access & caching to speed up debug builds
			auto& arrayGuts = reinterpret_cast<Implementation::ArrayGuts<T>&>(array);

			// No values to add, early exit
			if (!count)
				return arrayGuts.data + arrayGuts.size;

			// For arrays with an unknown deleter we'll always copy-allocate to a new place. Not using reallocate() as we don't
			// know where the original memory comes from.
			const std::size_t desiredCapacity = arrayGuts.size + count;
			std::size_t capacity;

			if (arrayGuts.deleter != Allocator::deleter) {
				capacity = Allocator::grow(nullptr, desiredCapacity);
				T* const newArray = Allocator::allocate(capacity);
				arrayMoveConstruct<T>(arrayGuts.data, newArray, arrayGuts.size);
				array = Array<T> { newArray, arrayGuts.size, Allocator::deleter };

				// Otherwise, if there's no space anymore, reallocate, which might be able to grow in-place
			} else {
				capacity = Allocator::capacity(arrayGuts.data);
				if (arrayGuts.size + count > capacity) {
					capacity = Allocator::grow(arrayGuts.data, desiredCapacity);
					Allocator::reallocate(arrayGuts.data, arrayGuts.size, capacity);
				}
			}

			// Increase array size and return the previous end pointer
			T* const it = arrayGuts.data + arrayGuts.size;
			arrayGuts.size += count;
			return it;
		}
	}

	template<class T, class Allocator> inline T& arrayAppend(Array<T>& array, const typename std::common_type<T>::type& value) {
		T* const it = Implementation::arrayGrowBy<T, Allocator>(array, 1);
		// Can't use {}, see the GCC 4.8-specific overload for details
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) &&  __GNUC__ < 5
		Implementation::construct(*it, value);
#else
		new(it) T { value };
#endif
		return *it;
	}

	template<class T, class Allocator> inline ArrayView<T> arrayAppend(Array<T>& array, const ArrayView<const T> values) {
		// Direct access & caching to speed up debug builds
		const std::size_t valueCount = values.size();

		T* const it = Implementation::arrayGrowBy<T, Allocator>(array, valueCount);
		Implementation::arrayCopyConstruct<T>(values.data(), it, valueCount);
		return { it, valueCount };
	}

	template<class T, class ...Args> inline T& arrayAppend(Array<T>& array, InPlaceInitT, Args&&... args) {
		return arrayAppend<T, ArrayAllocator<T>>(array, InPlaceInit, std::forward<Args>(args)...);
	}

	template<class T, class Allocator, class ...Args> T& arrayAppend(Array<T>& array, InPlaceInitT, Args&&... args) {
		T* const it = Implementation::arrayGrowBy<T, Allocator>(array, 1);
		// No helper function as there's no way we could memcpy such a thing.
		// On GCC 4.8 this includes another workaround, see the 4.8-specific overload docs for details
		Implementation::construct(*it, std::forward<Args>(args)...);
		return *it;
	}

	template<class T, class Allocator> ArrayView<T> arrayAppend(Array<T>& array, NoInitT, const std::size_t count) {
		T* const it = Implementation::arrayGrowBy<T, Allocator>(array, count);
		return { it, count };
	}

	namespace Implementation {

		template<class T> inline void arrayShiftForward(T* const src, T* const dst, const std::size_t count, typename std::enable_if<
			std::is_trivially_copyable<T>::value
		>::type* = nullptr) {
			// Compared to the non-trivially-copyable variant below, just delegate to memmove() and assume it can figure
			// out how to copy from back to front more efficiently that we ever could.
			// Same as with memcpy(), apparently memmove() can't be called with null pointers, even if size is zero. I call that bullying.
			if (count) std::memmove(dst, src, count * sizeof(T));
		}

		template<class T> inline void arrayShiftForward(T* const src, T* const dst, const std::size_t count, typename std::enable_if<!
			std::is_trivially_copyable<T>::value
		>::type* = nullptr) {
			static_assert(std::is_nothrow_move_constructible<T>::value && std::is_nothrow_move_assignable<T>::value,
				"nothrow move-constructible and move-assignable type is required");

			// Count of non-overlapping items, which will be move-constructed on one
			// side and destructed on the other. The rest will be move-assigned.
			const std::size_t nonOverlappingCount = src + count < dst ? count : dst - src;

			// Move-construct the non-overlapping elements. Doesn't matter if going forward or backward as we're not
			// overwriting anything, but go backward for consistency with the move-assignment loop below.
			for (T* end = src + count - nonOverlappingCount, *constructSrc = src + count, *constructDst = dst + count; constructSrc > end; --constructSrc, --constructDst) {
				// Can't use {}, see the GCC 4.8-specific overload for details
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) &&  __GNUC__ < 5
				Implementation::construct(*(constructDst - 1), std::move(*(constructSrc - 1)));
#else
				new(constructDst - 1) T { std::move(*(constructSrc - 1)) };
#endif
			}

			// Move-assign overlapping elements, going backwards to avoid overwriting values that are yet to be moved.
			// This loop is never entered if nonOverlappingCount >= count.
			for (T* assignSrc = src + count - nonOverlappingCount, *assignDst = dst + count - nonOverlappingCount; assignSrc > src; --assignSrc, --assignDst)
				*(assignDst - 1) = std::move(*(assignSrc - 1));

			// Destruct non-overlapping elements in the newly-formed gap so the calling code can assume uninitialized memory
			// both in all cases. Here it again doesn't matter if going forward or backward, but go backward for consistency.
			for (T* destructSrc = src + nonOverlappingCount; destructSrc != src; --destructSrc)
				(destructSrc - 1)->~T();
		}

		template<class T, class Allocator> T* arrayGrowAtBy(Array<T>& array, const std::size_t index, const std::size_t count) {
			// Direct access & caching to speed up debug builds
			auto& arrayGuts = reinterpret_cast<Implementation::ArrayGuts<T>&>(array);
			DEATH_ASSERT(index <= arrayGuts.size, "Containers::arrayInsert(): can't insert at index" << index << "into an array of size" << arrayGuts.size, arrayGuts.data);

			// No values to add, early exit
			if (!count)
				return arrayGuts.data + index;

			// For arrays with an unknown deleter we'll always move-allocate to a new place, the parts before and after
			// index separately. Not using reallocate() as we don't know where the original memory comes from.
			const std::size_t desiredCapacity = arrayGuts.size + count;
			std::size_t capacity;

			bool needsShiftForward = false;
			if (arrayGuts.deleter != Allocator::deleter) {
				capacity = Allocator::grow(nullptr, desiredCapacity);
				T* const newArray = Allocator::allocate(capacity);
				arrayMoveConstruct<T>(arrayGuts.data, newArray, index);
				arrayMoveConstruct<T>(arrayGuts.data + index, newArray + index + count, arrayGuts.size - index);
				array = Array<T> { newArray, arrayGuts.size, Allocator::deleter };

				// Otherwise, if there's no space anymore, reallocate. which might be able to grow in-place.
				// However we still need to shift the part after index forward.
			} else {
				capacity = Allocator::capacity(arrayGuts.data);
				if (arrayGuts.size + count > capacity) {
					capacity = Allocator::grow(arrayGuts.data, desiredCapacity);
					Allocator::reallocate(arrayGuts.data, arrayGuts.size, capacity);
				}

				needsShiftForward = true;
			}

			// Increase array size and return the position at index
			T* const it = arrayGuts.data + index;

			// Perform a shift of elements after index. Needs to be done after the ASan annotation is updated, otherwise it'll
			// trigger a failure due to outdated bounds information.
			if (needsShiftForward)
				arrayShiftForward(arrayGuts.data + index, arrayGuts.data + index + count, arrayGuts.size - index);

			arrayGuts.size += count;
			return it;
		}

	}

	template<class T, class Allocator> inline T& arrayInsert(Array<T>& array, std::size_t index, const typename std::common_type<T>::type& value) {
		T* const it = Implementation::arrayGrowAtBy<T, Allocator>(array, index, 1);
		// Can't use {}, see the GCC 4.8-specific overload for details
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) &&  __GNUC__ < 5
		Implementation::construct(*it, value);
#else
		new(it) T { value };
#endif
		return *it;
	}

	template<class T, class Allocator> inline ArrayView<T> arrayInsert(Array<T>& array, std::size_t index, const ArrayView<const T> values) {
		// Direct access & caching to speed up debug builds
		const std::size_t valueCount = values.size();

		T* const it = Implementation::arrayGrowAtBy<T, Allocator>(array, index, valueCount);
		Implementation::arrayCopyConstruct<T>(values.data(), it, valueCount);
		return { it, valueCount };
	}

	template<class T, class ...Args> inline T& arrayInsert(Array<T>& array, std::size_t index, InPlaceInitT, Args&&... args) {
		return arrayInsert<T, ArrayAllocator<T>>(array, index, InPlaceInit, std::forward<Args>(args)...);
	}

	template<class T, class Allocator, class ...Args> T& arrayInsert(Array<T>& array, std::size_t index, InPlaceInitT, Args&&... args) {
		T* const it = Implementation::arrayGrowAtBy<T, Allocator>(array, index, 1);
		// No helper function as there's no way we could memcpy such a thing.
		// On GCC 4.8 this includes another workaround, see the 4.8-specific overload docs for details
		Implementation::construct(*it, std::forward<Args>(args)...);
		return *it;
	}

	template<class T, class Allocator> ArrayView<T> arrayInsert(Array<T>& array, const std::size_t index, NoInitT, const std::size_t count) {
		T* const it = Implementation::arrayGrowAtBy<T, Allocator>(array, index, count);
		return { it, count };
	}

	namespace Implementation {

		template<class T> inline void arrayShiftBackward(T* const src, T* const dst, const std::size_t moveCount, std::size_t, typename std::enable_if<
			std::is_trivially_copyable<T>::value
		>::type* = nullptr) {
			// Compared to the non-trivially-copyable variant below, just delegate to memmove() and assume it can figure
			// out how to copy from front to back more efficiently that we ever could.
			// Same as with memcpy(), apparently memmove() can't be called with null pointers, even if size is zero. I call that bullying.
			if (moveCount) std::memmove(dst, src, moveCount * sizeof(T));
		}

		template<class T> inline void arrayShiftBackward(T* const src, T* const dst, const std::size_t moveCount, std::size_t destructCount, typename std::enable_if<!
			std::is_trivially_copyable<T>::value
		>::type* = nullptr) {
			static_assert(std::is_nothrow_move_constructible<T>::value && std::is_nothrow_move_assignable<T>::value,
				"nothrow move-constructible and move-assignable type is required");

			// Move-assign later elements to earlier
			for (T* end = src + moveCount, *assignSrc = src, *assignDst = dst; assignSrc != end; ++assignSrc, ++assignDst)
				*assignDst = std::move(*assignSrc);

			// Destruct remaining moved-out elements
			for (T* end = src + moveCount, *destructSrc = end - destructCount; destructSrc != end; ++destructSrc)
				destructSrc->~T();
		}

	}

	template<class T, class Allocator> void arrayRemove(Array<T>& array, const std::size_t index, const std::size_t count) {
		// Direct access to speed up debug builds
		auto& arrayGuts = reinterpret_cast<Implementation::ArrayGuts<T>&>(array);
		DEATH_ASSERT(index + count <= arrayGuts.size, "Containers::arrayRemove(): can't remove" << count << "elements at index" << index << "from an array of size" << arrayGuts.size, );

		// Nothing to remove, yay!
		if (!count) return;

		// If we don't have our own deleter, we need to reallocate in order to store the capacity. Move the parts before
		// and after the index separately, which will also cause the removed elements to be properly destructed, so nothing
		// else needs to be done. Not using reallocate() as we don't know where the original memory comes from.
		if (arrayGuts.deleter != Allocator::deleter) {
			T* const newArray = Allocator::allocate(arrayGuts.size - count);
			Implementation::arrayMoveConstruct<T>(arrayGuts.data, newArray, index);
			Implementation::arrayMoveConstruct<T>(arrayGuts.data + index + count, newArray + index, arrayGuts.size - index - count);
			array = Array<T> { newArray, arrayGuts.size - count, Allocator::deleter };

			// Otherwise shift the elements after index backward
		} else {
			Implementation::arrayShiftBackward(arrayGuts.data + index + count, arrayGuts.data + index, arrayGuts.size - index - count, count);
			arrayGuts.size -= count;
		}
	}

	template<class T, class Allocator> void arrayRemoveUnordered(Array<T>& array, const std::size_t index, const std::size_t count) {
		// Direct access to speed up debug builds
		auto& arrayGuts = reinterpret_cast<Implementation::ArrayGuts<T>&>(array);
		DEATH_ASSERT(index + count <= arrayGuts.size, "Containers::arrayRemoveUnordered(): can't remove" << count << "elements at index" << index << "from an array of size" << arrayGuts.size, );

		// Nothing to remove, yay!
		if (!count) return;

		// If we don't have our own deleter, we need to reallocate in order to store the capacity. Move the parts before
		// and after the index separately, which will also cause the removed elements to be properly destructed, so nothing
		// else needs to be done. Not using reallocate() as we don't know where the original memory comes from.
		if (arrayGuts.deleter != Allocator::deleter) {
			T* const newArray = Allocator::allocate(arrayGuts.size - count);
			Implementation::arrayMoveConstruct<T>(arrayGuts.data, newArray, index);
			Implementation::arrayMoveConstruct<T>(arrayGuts.data + index + count, newArray + index, arrayGuts.size - index - count);
			array = Array<T> { newArray, arrayGuts.size - count, Allocator::deleter };

			// Otherwise move the last count elements over the ones at index, or less if there's not that many after the removed range
		} else {
			const std::size_t moveCount = std::min(count, arrayGuts.size - count - index);
			Implementation::arrayShiftBackward(arrayGuts.data + arrayGuts.size - moveCount, arrayGuts.data + index, moveCount, count);
			arrayGuts.size -= count;
		}
	}

	template<class T, class Allocator> void arrayRemoveSuffix(Array<T>& array, const std::size_t count) {
		// Direct access to speed up debug builds
		auto& arrayGuts = reinterpret_cast<Implementation::ArrayGuts<T>&>(array);
		DEATH_ASSERT(count <= arrayGuts.size, "Containers::arrayRemoveSuffix(): can't remove" << count << "elements from an array of size" << arrayGuts.size, );

		// Nothing to remove, yay!
		if (!count) return;

		// If we don't have our own deleter, we need to reallocate in order to store the capacity. That'll also cause the excessive
		// elements to be properly destructed, so nothing else needs to be done. Not using reallocate() as we don't know where the original memory comes from.
		if (arrayGuts.deleter != Allocator::deleter) {
			T* const newArray = Allocator::allocate(arrayGuts.size - count);
			Implementation::arrayMoveConstruct<T>(arrayGuts.data, newArray, arrayGuts.size - count);
			array = Array<T> { newArray, arrayGuts.size - count, Allocator::deleter };

			// Otherwise call the destructor on the excessive elements and update the size
		} else {
			Implementation::arrayDestruct<T>(arrayGuts.data + arrayGuts.size - count, arrayGuts.data + arrayGuts.size);
			arrayGuts.size -= count;
		}
	}

	template<class T, class Allocator> void arrayShrink(Array<T>& array, NoInitT) {
		// Direct access to speed up debug builds
		auto& arrayGuts = reinterpret_cast<Implementation::ArrayGuts<T>&>(array);

		// If not using our growing allocator, assume the array size equals its capacity and do nothing
		if (arrayGuts.deleter != Allocator::deleter)
			return;

		// Even if we don't need to shrink, reallocating to an usual array with common deleters to avoid surprises
		Array<T> newArray { NoInit, arrayGuts.size };
		Implementation::arrayMoveConstruct<T>(arrayGuts.data, newArray, arrayGuts.size);
		array = std::move(newArray);
	}

	template<class T, class Allocator> void arrayShrink(Array<T>& array, DefaultInitT) {
		// Direct access to speed up debug builds */
		auto& arrayGuts = reinterpret_cast<Implementation::ArrayGuts<T>&>(array);

		// If not using our growing allocator, assume the array size equals its capacity and do nothing
		if (arrayGuts.deleter != Allocator::deleter)
			return;

		// Even if we don't need to shrink, reallocating to an usual array with common deleters to avoid surprises
		Array<T> newArray { DefaultInit, arrayGuts.size };
		Implementation::arrayMoveAssign<T>(arrayGuts.data, newArray, arrayGuts.size);
		array = std::move(newArray);
	}
}