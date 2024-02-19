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

#include "Array.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

// No __has_feature() on GCC: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=60512
// Using a dedicated macro instead: https://stackoverflow.com/a/34814667
#if !defined(DEATH_CONTAINERS_NO_SANITIZER_ANNOTATIONS)
#	if defined(__has_feature)
#		if __has_feature(address_sanitizer)
#			define _DEATH_CONTAINERS_SANITIZER_ENABLED
#		endif
#	endif
#	if defined(__SANITIZE_ADDRESS__)
#		define _DEATH_CONTAINERS_SANITIZER_ENABLED
#	endif
#endif

#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
// https://github.com/llvm/llvm-project/blob/main/compiler-rt/include/sanitizer/common_interface_defs.h
extern "C" void __sanitizer_annotate_contiguous_container(const void* beg, const void* end, const void* old_mid, const void* new_mid)
	// Declaration of this function in <vector> in MSVC 2022 14.35 and earlier STL includes a noexcept for some strange unexplained reason,
	// which makes the signature differ from Clang's. See the PR comment here:
	//	https://github.com/microsoft/STL/pull/2071/commits/daa4db9bf10400678438d9c6b33630c7947e469e
	// It got then subsequently removed, but without any additional explanation in the commit message nor links
	// to corresponding bug reports and the STL repo has no tags or mapping to actual releases:
	//	https://github.com/microsoft/STL/pull/3164/commits/9f503ca22bcc32cd885184ea754ec4223759c431
	// So I only accidentally discovered that this commit is in 14.36:
	//  https://developercommunity.visualstudio.com/t/__sanitizer_annotate_contiguous_containe/10119696
	// The difference in noexcept is only a problem with `/std:c++17` (where noexcept becomes a part of the function signature) *and* with
	// the `/permissive-` flag set, or `/std:c++20` alone (where the flag is implicitly enabled).
#	if defined(DEATH_TARGET_DINKUMWARE) && _MSC_VER < 1936
	noexcept
#	endif
	;
#endif

namespace Death { namespace Containers {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

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
	template<class T> struct ArrayNewAllocator {
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
		static_assert(std::is_trivially_copyable<T>::value, "Only trivially copyable types are usable with this allocator");

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
			// Compared to ArrayNewAllocator, here the capacity is stored in bytes so it's possible to "reinterpret"
			// the array into a different type (as the deleter is a typeless std::free() in any case)
			const std::size_t inBytes = capacity * sizeof(T) + AllocationOffset;
			char* const memory = static_cast<char*>(std::malloc(inBytes));
			DEATH_ASSERT(memory != nullptr, {}, "Containers::ArrayMallocAllocator: Can't allocate %zu bytes", inBytes);
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
			if (data != nullptr) std::free(reinterpret_cast<char*>(data) - AllocationOffset);
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
	template<class T> using ArrayAllocator = typename std::conditional<std::is_trivially_copyable<T>::value, ArrayMallocAllocator<T>, ArrayNewAllocator<T>>::type;

	/**
		@brief Reinterpret-cast a growable array

		If the array is growable using @ref ArrayMallocAllocator (which is aliased to
		@ref ArrayAllocator for all trivially-copyable types), the deleter is a simple
		call to a typeless @ref std::free(). This makes it possible to change the array
		type without having to use a different deleter, losing the growable property in
		the process.

		Equivalently to @ref arrayCast(), the size of the new array is calculated as
		@cpp view.size()*sizeof(T)/sizeof(U) @ce. Expects that both types are
		trivially copyable and [standard layout](http://en.cppreference.com/w/cpp/concept/StandardLayoutType)
		and the total byte size doesn't change.
	*/
	template<class U, class T> Array<U> arrayAllocatorCast(Array<T>&& array);

	/** @overload */
	template<class U, template<class> class Allocator, class T> Array<U> arrayAllocatorCast(Array<T>&& array) {
		static_assert(std::is_standard_layout<T>::value, "The source type is not standard layout");
		static_assert(std::is_standard_layout<U>::value, "The target type is not standard layout");
		static_assert(std::is_trivially_copyable<T>::value && std::is_trivially_copyable<U>::value, "Only trivially copyable types can use the allocator cast");

		// If the array is default-constructed or just generally empty with the default deleter, just pass it through without changing anything.
		// This behavior is consistent with calling `arrayResize(array, 0)`, `arrayReserve(array, 0)` and such, which also just pass empty arrays
		// through without affecting their deleter.
		if (array.isEmpty() && !array.data() && !array.deleter())
			return {};

		// Unlike arrayInsert() etc, this is not called that often and should be as checked as possible, so it's not a debug assert
		DEATH_ASSERT(array.deleter() == Allocator<T>::deleter && (std::is_base_of<ArrayMallocAllocator<T>, Allocator<T>>::value), {},
			"Containers::arrayAllocatorCast(): The array has to use the ArrayMallocAllocator or a derivative");
		const std::size_t size = array.size() * sizeof(T) / sizeof(U);
		DEATH_ASSERT(size * sizeof(U) == array.size() * sizeof(T), {},
			"Containers::arrayAllocatorCast(): Can't reinterpret %zu %zu-byte items into a %zu-byte type", array.size(), sizeof(T), sizeof(U));
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

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class T> class Allocator, class T> inline bool arrayIsGrowable(Array<T>& array) {
		return arrayIsGrowable<T, Allocator<T>>(array);
	}

	/**
		@brief Array capacity

		For a growable array using given @p Allocator returns its capacity, otherwise
		returns @ref Array::size().

		This function is equivalent to calling @relativeref{std::vector,capacity()} on
		a @ref std::vector.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> std::size_t arrayCapacity(Array<T>& array);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class T> class Allocator, class T> inline std::size_t arrayCapacity(Array<T>& array) {
		return arrayCapacity<T, Allocator<T>>(array);
	}

	/**
		@brief Reserve given capacity in an array
		@return New capacity of the array

		If @p array capacity is already large enough, the function returns the current
		capacity. Otherwise the memory is reallocated to desired @p capacity, with the
		@ref Array::size() staying the same, and @p capacity returned back. Note that
		in case the array is non-growable of sufficient size, it's kept as such,
		without being reallocated to a growable version.

		Complexity is at most @f$ \mathcal{O}(n) @f$ in the size of the original
		container, @f$ \mathcal{O}(1) @f$ if the capacity is already large enough or
		if the reallocation can be done in-place. On top of what the @p Allocator (or
		the default @ref ArrayAllocator) itself needs, @p T is required to be nothrow
		move-constructible and move-assignable.

		This function is equivalent to calling @relativeref{std::vector,reserve()} on
		a @ref std::vector.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> std::size_t arrayReserve(Array<T>& array, std::size_t capacity);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class T> class Allocator, class T> inline std::size_t arrayReserve(Array<T>& array, std::size_t capacity) {
		return arrayReserve<T, Allocator<T>>(array, capacity);
	}

	/**
		@brief Resize an array to given size, default-initializing new elements

		If the array is growable and capacity is large enough, calls a destructor on
		elements that get cut off the end (if any, and if @p T is not trivially
		destructible, in which case nothing is done) and returns. Otherwise, the memory
		is reallocated to desired @p size. After that, new elements at the end of the
		array are default-initialized using placement-new (and nothing done for trivial
		types). Note that in case the array is non-growable of exactly the requested
		size, it's kept as such, without being reallocated to a growable version.

		Complexity is at most @f$ \mathcal{O}(n) @f$ in the size of the new container,
		@f$ \mathcal{O}(1) @f$ if current container size is already exactly of given
		size. On top of what the @p Allocator (or the default @ref ArrayAllocator)
		itself needs, @p T is required to be nothrow move-constructible and
		default-constructible.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> void arrayResize(Array<T>& array, DefaultInitT, std::size_t size);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline void arrayResize(Array<T>& array, DefaultInitT, std::size_t size) {
		arrayResize<T, Allocator<T>>(array, DefaultInit, size);
	}

	/**
		@brief Resize an array to given size, value-initializing new elements

		Similar to @ref arrayResize(Array<T>&, DefaultInitT, std::size_t) except that
		the new elements at the end are not default-initialized, but value-initialized
		(i.e., trivial types zero-initialized and default constructor called
		otherwise).

		On top of what the @p Allocator (or the default @ref ArrayAllocator) itself
		needs, @p T is required to be nothrow move-constructible and
		default-constructible.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> void arrayResize(Array<T>& array, ValueInitT, std::size_t size);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline void arrayResize(Array<T>& array, ValueInitT, std::size_t size) {
		arrayResize<T, Allocator<T>>(array, ValueInit, size);
	}

	/**
		@brief Resize an array to given size, value-initializing new elements

		Alias to @ref arrayResize(Array<T>&, ValueInitT, std::size_t).

		This function is equivalent to calling @relativeref{std::vector,resize()} on
		a @ref std::vector.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> inline void arrayResize(Array<T>& array, std::size_t size) {
		return arrayResize<T, Allocator>(array, ValueInit, size);
	}

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline void arrayResize(Array<T>& array, std::size_t size) {
		arrayResize<T, Allocator<T>>(array, size);
	}

	/**
		@brief Resize an array to given size, keeping new elements uninitialized

		Similar to @ref arrayResize(Array<T>&, DefaultInitT, std::size_t) except that
		the new elements at the end are not default-initialized, but left in an
		uninitialized state instead. I.e., placement-new is meant to be used on *all*
		newly added elements with a non-trivially-copyable @p T.

		On top of what the @p Allocator (or the default @ref ArrayAllocator) itself
		needs, @p T is required to be nothrow move-constructible.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> void arrayResize(Array<T>& array, NoInitT, std::size_t size);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline void arrayResize(Array<T>& array, NoInitT, std::size_t size) {
		arrayResize<T, Allocator<T>>(array, NoInit, size);
	}

	/**
		@brief Resize an array to given size, constructing new elements using provided arguments

		Similar to @ref arrayResize(Array<T>&, ValueInitT, std::size_t) except that
		the new elements at the end are constructed using placement-new with provided
		@p args.

		On top of what the @p Allocator (or the default @ref ArrayAllocator) itself
		needs, @p T is required to be nothrow move-constructible and constructible from
		provided @p args.
	*/
	template<class T, class ...Args> void arrayResize(Array<T>& array, DirectInitT, std::size_t size, Args&&... args);

	/** @overload */
	template<class T, class Allocator, class ...Args> void arrayResize(Array<T>& array, DirectInitT, std::size_t size, Args&&... args);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T, class ...Args> inline void arrayResize(Array<T>& array, DirectInitT, std::size_t size, Args&&... args) {
		arrayResize<T, Allocator<T>>(array, DirectInit, size, std::forward<Args>(args)...);
	}

	/**
		@brief Resize an array to given size, copy-constructing new elements using the provided value

		Calls @ref arrayResize(Array<T>&, DirectInitT, std::size_t, Args&&... args)
		with @p value.

		This function is equivalent to calling @relativeref{std::vector,resize()} on
		a @ref std::vector.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> inline void arrayResize(Array<T>& array, std::size_t size, const typename std::common_type<T>::type& value) {
		arrayResize<T, Allocator>(array, DirectInit, size, value);
	}

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline void arrayResize(Array<T>& array, std::size_t size, const typename std::common_type<T>::type& value) {
		arrayResize<T, Allocator<T>>(array, size, value);
	}

	/**
		@brief Copy-append an item to an array
		@return Reference to the newly appended item

		If the array is not growable or the capacity is not large enough, the array
		capacity is grown first according to rules described in the
		@ref ArrayAllocator::grow() "grow()" function of a particular allocator. Then,
		@p value is copy-constructed at the end of the array and @ref Array::size()
		increased by 1.

		Amortized complexity is @f$ \mathcal{O}(1) @f$ providing the allocator growth
		ratio is exponential. On top of what the @p Allocator (or the default
		@ref ArrayAllocator) itself needs, @p T is required to be nothrow
		move-constructible and copy-constructible.

		This function is equivalent to calling @relativeref{std::vector,push_back()} on
		a @ref std::vector.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> T& arrayAppend(Array<T>& array, const typename std::common_type<T>::type& value);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline T& arrayAppend(Array<T>& array, const typename std::common_type<T>::type& value) {
		return arrayAppend<T, Allocator<T>>(array, value);
	}

	/**
		@brief In-place append an item to an array
		@return Reference to the newly appended item

		Similar to @ref arrayAppend(Array<T>&, const typename std::common_type<T>::type&)
		except that the new element is constructed using placement-new with provided
		@p args.

		On top of what the @p Allocator (or the default @ref ArrayAllocator) itself
		needs, @p T is required to be nothrow move-constructible and constructible from
		provided @p args.

		This function is equivalent to calling @relativeref{std::vector,emplace_back()}
		on a @ref std::vector.
	*/
	template<class T, class ...Args> T& arrayAppend(Array<T>& array, InPlaceInitT, Args&&... args);

	/** @overload */
	template<class T, class Allocator, class ...Args> T& arrayAppend(Array<T>& array, InPlaceInitT, Args&&... args);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T, class ...Args> inline T& arrayAppend(Array<T>& array, InPlaceInitT, Args&&... args) {
		return arrayAppend<T, Allocator<T>>(array, InPlaceInit, std::forward<Args>(args)...);
	}

	/**
		@brief Move-append an item to an array
		@return Reference to the newly appended item

		Calls @ref arrayAppend(Array<T>&, InPlaceInitT, Args&&... args) with @p value.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> inline T& arrayAppend(Array<T>& array, typename std::common_type<T>::type&& value) {
		return arrayAppend<T, Allocator>(array, InPlaceInit, std::move(value));
	}

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline T& arrayAppend(Array<T>& array, typename std::common_type<T>::type&& value) {
		return arrayAppend<T, Allocator<T>>(array, InPlaceInit, std::move(value));
	}

	/**
		@brief Copy-append a list of items to an array
		@return View on the newly appended items

		Like @ref arrayAppend(Array<T>&, const typename std::common_type<T>::type&),
		but inserting multiple values at once.

		On top of what the @p Allocator (or the default @ref ArrayAllocator) itself
		needs, @p T is required to be nothrow move-constructible and
		copy-constructible.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> ArrayView<T> arrayAppend(Array<T>& array, typename std::common_type<ArrayView<const T>>::type values);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline ArrayView<T> arrayAppend(Array<T>& array, typename std::common_type<ArrayView<const T>>::type values) {
		return arrayAppend<T, Allocator<T>>(array, values);
	}

	/** @overload */
	template<class T, class Allocator = ArrayAllocator<T>> inline ArrayView<T> arrayAppend(Array<T>& array, std::initializer_list<typename std::common_type<T>::type> values) {
		return arrayAppend<T, Allocator>(array, arrayView(values));
	}

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline ArrayView<T> arrayAppend(Array<T>& array, std::initializer_list<typename std::common_type<T>::type> values) {
		return arrayAppend<T, Allocator<T>>(array, values);
	}

	/**
		@brief Append given count of uninitialized values to an array
		@return View on the newly appended items

		A lower-level variant of @ref arrayAppend(Array<T>&, typename std::common_type<ArrayView<const T>>::type)
		where the new values are meant to be initialized in-place after, instead of
		being copied from a pre-existing location. The new values are always
		uninitialized --- i.e., placement-new is meant to be used on *all* inserted
		elements with a non-trivially-copyable @p T.

		On top of what the @p Allocator (or the default @ref ArrayAllocator) itself
		needs, @p T is required to be nothrow move-constructible.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> ArrayView<T> arrayAppend(Array<T>& array, NoInitT, std::size_t count);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline ArrayView<T> arrayAppend(Array<T>& array, NoInitT, std::size_t count) {
		return arrayAppend<T, Allocator<T>>(array, NoInit, count);
	}

	/**
		@brief Copy-insert an item into an array
		@return Reference to the newly inserted item

		Expects that @p index is not larger than @ref Array::size(). If the array is
		not growable or the capacity is not large enough, the array capacity is grown
		first according to rules described in the
		@ref ArrayAllocator::grow() "grow()" function of a particular allocator. Then,
		items starting at @p index are moved one item forward, @p value is copied to
		@p index and @ref Array::size() is increased by 1.

		Amortized complexity is @f$ \mathcal{O}(n) @f$. On top of what the @p Allocator
		(or the default @ref ArrayAllocator) itself needs, @p T is required to be
		nothrow move-constructible, nothrow move-assignable and copy-constructible.

		This function is equivalent to calling @relativeref{std::vector,insert()} on
		a @ref std::vector.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> T& arrayInsert(Array<T>& array, std::size_t index, const typename std::common_type<T>::type& value);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> T& arrayInsert(Array<T>& array, std::size_t index, const typename std::common_type<T>::type& value) {
		return arrayInsert<T, Allocator<T>>(array, index, value);
	}

	/**
		@brief In-place insert an item into an array
		@return Reference to the newly inserted item

		Similar to @ref arrayInsert(Array<T>&, std::size_t, const typename std::common_type<T>::type&)
		except that the new element is constructed using placement-new with provided
		@p args.

		On top of what the @p Allocator (or the default @ref ArrayAllocator) itself
		needs, @p T is required to be nothrow move-constructible, nothrow
		move-assignable and constructible from provided @p args.

		This function is equivalent to calling @relativeref{std::vector,emplace()}
		on a @ref std::vector.
	*/
	template<class T, class ...Args> T& arrayInsert(Array<T>& array, std::size_t index, InPlaceInitT, Args&&... args);

	/** @overload */
	template<class T, class Allocator, class ...Args> T& arrayInsert(Array<T>& array, std::size_t index, InPlaceInitT, Args&&... args);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T, class ...Args> T& arrayInsert(Array<T>& array, std::size_t index, InPlaceInitT, Args&&... args) {
		return arrayInsert<T, Allocator<T>>(array, index, std::forward<Args>(args)...);
	}

	/**
		@brief Move-insert an item into an array
		@return Reference to the newly appended item

		Calls @ref arrayInsert(Array<T>&, std::size_t, InPlaceInitT, Args&&... args)
		with @p value.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> inline T& arrayInsert(Array<T>& array, std::size_t index, typename std::common_type<T>::type&& value) {
		return arrayInsert<T, Allocator>(array, index, InPlaceInit, std::move(value));
	}

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline T& arrayInsert(Array<T>& array, std::size_t index, typename std::common_type<T>::type&& value) {
		return arrayInsert<T, Allocator<T>>(array, index, InPlaceInit, std::move(value));
	}

	/**
		@brief Copy-insert a list of items into an array
		@return View on the newly appended items

		Like @ref arrayInsert(Array<T>&, std::size_t, const typename std::common_type<T>::type&),
		but inserting multiple values at once.

		Amortized complexity is @f$ \mathcal{O}(m + n) @f$, where @f$ m @f$ is the
		number of items being inserted and @f$ n @f$ is the existing array size. On top
		of what the @p Allocator (or the default @ref ArrayAllocator) itself needs,
		@p T is required to be nothrow move-constructible, nothrow move-assignable and
		copy-constructible.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> ArrayView<T> arrayInsert(Array<T>& array, std::size_t index, typename std::common_type<ArrayView<const T>>::type values);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline ArrayView<T> arrayInsert(Array<T>& array, std::size_t index, typename std::common_type<ArrayView<const T>>::type values) {
		return arrayInsert<T, Allocator<T>>(array, index, values);
	}

	/** @overload */
	template<class T, class Allocator = ArrayAllocator<T>> ArrayView<T>  arrayInsert(Array<T>& array, std::size_t index, std::initializer_list<typename std::common_type<T>::type> values) {
		return arrayInsert<T, Allocator>(array, index, arrayView(values));
	}

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline ArrayView<T>  arrayInsert(Array<T>& array, std::size_t index, std::initializer_list<typename std::common_type<T>::type> values) {
		return arrayInsert<T, Allocator<T>>(array, index, values);
	}

	/**
		@brief Insert given count of uninitialized values into an array
		@return View on the newly appended items

		A lower-level variant of @ref arrayInsert(Array<T>&, std::size_t, typename std::common_type<ArrayView<const T>>::type)
		where the new values are meant to be initialized in-place after, instead of
		being copied from a pre-existing location. Independently of whether the array
		was reallocated to fit the new items or the items were just shifted around
		because the capacity was large enough, the new values are always uninitialized
		--- i.e., placement-new is meant to be used on *all* inserted elements with a
		non-trivially-copyable @p T.

		Amortized complexity is @f$ \mathcal{O}(n) @f$, where @f$ n @f$ is the existing
		array size. On top of what the @p Allocator (or the default @ref ArrayAllocator)
		itself needs, @p T is required to be nothrow move-constructible and nothrow
		move-assignable.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> ArrayView<T> arrayInsert(Array<T>& array, std::size_t index, NoInitT, std::size_t count);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline ArrayView<T> arrayInsert(Array<T>& array, std::size_t index, NoInitT, std::size_t count) {
		return arrayInsert<T, Allocator<T>>(array, index, NoInit, count);
	}

	/**
		@brief Remove an element from an array

		Expects that @cpp index + count @ce is not larger than @ref Array::size(). If
		the array is not growable, all elements except the removed ones are reallocated
		to a growable version. Otherwise, items starting at @cpp index + count @ce are
		moved @cpp count @ce items backward and the @ref Array::size() is decreased by
		@p count.

		Amortized complexity is @f$ \mathcal{O}(m + n) @f$ where @f$ m @f$ is the
		number of items being removed and @f$ n @f$ is the array size after removal. On
		top of what the @p Allocator (or the default @ref ArrayAllocator) itself needs,
		@p T is required to be nothrow move-constructible and nothrow move-assignable.

		This function is equivalent to calling @relativeref{std::vector,erase()} on a
		@ref std::vector.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> void arrayRemove(Array<T>& array, std::size_t index, std::size_t count = 1);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline void arrayRemove(Array<T>& array, std::size_t index, std::size_t count = 1) {
		arrayRemove<T, Allocator<T>>(array, index, count);
	}

	/**
		@brief Remove an element from an unordered array

		A variant of @ref arrayRemove() that is more efficient in case the order of
		items in the array doesn't have to be preserved. Expects that
		@cpp index + count @ce is not larger than @ref Array::size(). If the array is
		not growable, all elements except the removed ones are reallocated to a
		growable version. Otherwise, the last @cpp min(count, array.size() - index - count) @ce
		items are moved over the items at @p index and the @ref Array::size() is
		decreased by @p count.

		Amortized complexity is @f$ \mathcal{O}(m) @f$ where @f$ m @f$ is the number of
		items being removed. On top of what the @p Allocator (or the default
		@ref ArrayAllocator) itself needs, @p T is required to be nothrow
		move-constructible and nothrow move-assignable.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> void arrayRemoveUnordered(Array<T>& array, std::size_t index, std::size_t count = 1);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline void arrayRemoveUnordered(Array<T>& array, std::size_t index, std::size_t count = 1) {
		arrayRemoveUnordered<T, Allocator<T>>(array, index, count);
	}

	/**
		@brief Remove a suffix from an array

		Expects that @p count is not larger than @ref Array::size(). If the array is
		not growable, all its elements except the removed suffix are reallocated to a
		growable version. Otherwise, a destructor is called on removed elements and the
		@ref Array::size() is decreased by @p count.

		Amortized complexity is @f$ \mathcal{O}(m) @f$ where @f$ m @f$ is the number of
		items removed. On top of what the @p Allocator (or the default
		@ref ArrayAllocator) itself needs, @p T is required to be nothrow
		move-constructible.

		With @p count set to @cpp 1 @ce, this function is equivalent to calling
		@relativeref{std::vector,pop_back()} on a @ref std::vector.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> void arrayRemoveSuffix(Array<T>& array, std::size_t count = 1);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
	template<template<class> class Allocator, class T> inline void arrayRemoveSuffix(Array<T>& array, std::size_t count = 1) {
		arrayRemoveSuffix<T, Allocator<T>>(array, count);
	}

	/**
		@brief Convert an array back to non-growable

		Allocates a @ref NoInit array that's exactly large enough to fit
		@ref Array::size() elements, move-constructs the elements there and frees the
		old memory using @ref Array::deleter(). If the array is not growable using
		given @p Allocator, it's assumed to be already as small as possible, and
		nothing is done.

		Complexity is at most @f$ \mathcal{O}(n) @f$ in the size of the container,
		@f$ \mathcal{O}(1) @f$ if the array is already non-growable. No constraints
		on @p T from @p Allocator (or the default @ref ArrayAllocator) apply here but
		@p T is required to be nothrow move-constructible.

		This function is equivalent to calling @relativeref{std::vector,shrink_to_fit()}
		on a @ref std::vector.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> void arrayShrink(Array<T>& array, NoInitT = NoInit);

	/**
		@brief Convert an array back to non-growable using a default initialization

		Allocates a @ref DefaultInit array that's exactly large enough to fit
		@ref Array::size() elements, move-assigns the elements there and frees the old
		memory using @ref Array::deleter(). If the array is not growable using
		given @p Allocator, it's assumed to be already as small as possible, and
		nothing is done.

		Complexity is at most @f$ \mathcal{O}(n) @f$ in the size of the container,
		@f$ \mathcal{O}(1) @f$ if the array is already non-growable. No constraints on
		@p T from @p Allocator (or the default @ref ArrayAllocator) apply here but @p T
		is required to be default-constructible and nothrow move-assignable.

		Compared to @ref arrayShrink(Array<T>&, NoInitT), the resulting array instance
		always has a default (@cpp nullptr @ce) deleter. This is useful when it's not
		possible to use custom deleters, such as in plugin implementations.
	*/
	template<class T, class Allocator = ArrayAllocator<T>> void arrayShrink(Array<T>& array, DefaultInitT);

	/**
		@overload

		Convenience overload allowing to specify just the allocator template, with
		array type being inferred.
	*/
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
			if (count != 0) std::memcpy(dst, src, count * sizeof(T));
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
				new(dst) T{std::move(*src)};
#endif
		}

		template<class T> inline void arrayMoveAssign(T* const src, T* const dst, const std::size_t count, typename std::enable_if<
			std::is_trivially_copyable<T>::value
		>::type* = nullptr) {
			// Apparently memcpy() can't be called with null pointers, even if size is zero. I call that bullying.
			if (count != 0) std::memcpy(dst, src, count * sizeof(T));
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
			if (count != 0) std::memcpy(dst, src, count * sizeof(T));
		}

		template<class T> inline void arrayCopyConstruct(const T* src, T* dst, const std::size_t count, typename std::enable_if<!
			std::is_trivially_copyable<T>::value
		>::type* = nullptr) {
			for (const T* end = src + count; src != end; ++src, ++dst)
				// Can't use {}, see the GCC 4.8-specific overload for details
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) &&  __GNUC__ < 5
				Implementation::construct(*dst, *src);
#else
				new(dst) T{*src};
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
			new(dst) T{std::move(*src)};
#endif
		for (T* it = array, *end = array + prevSize; it < end; ++it) it->~T();
		deallocate(array);
		array = newArray;
	}

	template<class T> void ArrayMallocAllocator<T>::reallocate(T*& array, std::size_t, const std::size_t newCapacity) {
		const std::size_t inBytes = newCapacity * sizeof(T) + AllocationOffset;
		char* const memory = static_cast<char*>(std::realloc(reinterpret_cast<char*>(array) - AllocationOffset, inBytes));
		DEATH_ASSERT(memory != nullptr, , "Containers::ArrayMallocAllocator: Can't reallocate %zu bytes", inBytes);
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
		const bool hasGrowingDeleter = (arrayGuts.deleter == Allocator::deleter);

		// If the capacity is large enough, nothing to do (even if we have the array allocated by something different)
		const std::size_t currentCapacity = arrayCapacity<T, Allocator>(array);
		if (currentCapacity >= capacity) return currentCapacity;

		// Otherwise allocate a new array, move the previous data there and replace the old Array instance with it. Array's deleter
		// will take care of destructing & deallocating the previous memory.
		if (!hasGrowingDeleter) {
			T* newArray = Allocator::allocate(capacity);
			Implementation::arrayMoveConstruct<T>(arrayGuts.data, newArray, arrayGuts.size);
			array = Array<T>{newArray, arrayGuts.size, Allocator::deleter};
		} else Allocator::reallocate(arrayGuts.data, arrayGuts.size, capacity);

#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
		__sanitizer_annotate_contiguous_container(
			Allocator::base(arrayGuts.data),
			arrayGuts.data + capacity,
			arrayGuts.data + capacity, // ASan assumes this for new allocations
			arrayGuts.data + arrayGuts.size);
#endif

		return capacity;
	}

	template<class T, class Allocator> void arrayResize(Array<T>& array, NoInitT, const std::size_t size) {
		// Direct access & value caching to speed up debug builds
		auto& arrayGuts = reinterpret_cast<Implementation::ArrayGuts<T>&>(array);
		const bool hasGrowingDeleter = (arrayGuts.deleter == Allocator::deleter);

		// New size is the same as the old one, nothing to do
		if (arrayGuts.size == size) return;

		// Reallocate if we don't have our growable deleter, as the default deleter  might then call destructors even in the non-initialized area ...
		if (!hasGrowingDeleter) {
			T* newArray = Allocator::allocate(size);
			Implementation::arrayMoveConstruct<T>(array, newArray,
				// Move the min of the two sizes -- if we shrink, move only what will fit in the new array; if we extend,
				// move only what's initialized in the original and left the rest not initialized
				arrayGuts.size < size ? arrayGuts.size : size);
			array = Array<T>{newArray, size, Allocator::deleter};

#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
			__sanitizer_annotate_contiguous_container(
				Allocator::base(arrayGuts.data),
				arrayGuts.data + arrayGuts.size,
				arrayGuts.data + arrayGuts.size,
				arrayGuts.data + arrayGuts.size);
#endif

		// ... or the desired size is larger than the capacity. In that case make use of the reallocate() function that might be able to grow in-place.
		} else if (Allocator::capacity(array) < size) {
			Allocator::reallocate(arrayGuts.data,
				// Move the min of the two sizes -- if we shrink, move only what will fit in the new array; if we extend,
				// move only what's initialized in the original and left the rest not initialized
				arrayGuts.size < size ? arrayGuts.size : size, size);
			arrayGuts.size = size;

#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
			__sanitizer_annotate_contiguous_container(
				Allocator::base(arrayGuts.data),
				arrayGuts.data + arrayGuts.size,
				arrayGuts.data + arrayGuts.size,
				arrayGuts.data + arrayGuts.size);
#endif

		// Otherwise call a destructor on the extra elements. If we get here, we have our growable deleter and didn't
		// need to reallocate (which would make this unnecessary).
		} else {
			Implementation::arrayDestruct<T>(arrayGuts.data + size, arrayGuts.data + arrayGuts.size);
			// This is a NoInit resize, so not constructing the new elements, only updating the size
#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
			__sanitizer_annotate_contiguous_container(
				Allocator::base(arrayGuts.data),
				arrayGuts.data + Allocator::capacity(array),
				arrayGuts.data + arrayGuts.size,
				arrayGuts.data + size);
#endif
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

	namespace Implementation
	{
		template<class T, class Allocator> T* arrayGrowBy(Array<T>& array, const std::size_t count) {
			// Direct access & caching to speed up debug builds
			auto& arrayGuts = reinterpret_cast<Implementation::ArrayGuts<T>&>(array);

			// No values to add, early exit
			if (count == 0)
				return arrayGuts.data + arrayGuts.size;

			// For arrays with an unknown deleter we'll always copy-allocate to a new place. Not using reallocate() as we don't
			// know where the original memory comes from.
			const std::size_t desiredCapacity = arrayGuts.size + count;
			std::size_t capacity;
#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
			T* oldMid = nullptr;
#endif
			if (arrayGuts.deleter != Allocator::deleter) {
				capacity = Allocator::grow(nullptr, desiredCapacity);
				T* const newArray = Allocator::allocate(capacity);
				arrayMoveConstruct<T>(arrayGuts.data, newArray, arrayGuts.size);
				array = Array<T>{newArray, arrayGuts.size, Allocator::deleter};

			// Otherwise, if there's no space anymore, reallocate, which might be able to grow in-place
			} else {
				capacity = Allocator::capacity(arrayGuts.data);
				if (arrayGuts.size + count > capacity) {
					capacity = Allocator::grow(arrayGuts.data, desiredCapacity);
					Allocator::reallocate(arrayGuts.data, arrayGuts.size, capacity);
				} else {
#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
					oldMid = arrayGuts.data + arrayGuts.size;
#endif
				}
			}

			// Increase array size and return the previous end pointer
			T* const it = arrayGuts.data + arrayGuts.size;
#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
			__sanitizer_annotate_contiguous_container(
				Allocator::base(arrayGuts.data),
				arrayGuts.data + capacity,
				// For a new allocation, ASan assumes the previous middle pointer is at the end of the array. If we grew an existing
				// allocation, the previous middle is set what __sanitier_acc() received as a middle value before.
				oldMid ? oldMid : arrayGuts.data + capacity,
				arrayGuts.data + arrayGuts.size + count);
#endif
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
		new(it) T{value};
#endif
		return *it;
	}

	template<class T, class Allocator> inline ArrayView<T> arrayAppend(Array<T>& array, const typename std::common_type<ArrayView<const T>>::type values) {
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

	namespace Implementation
	{
		template<class T> inline void arrayShiftForward(T* const src, T* const dst, const std::size_t count, typename std::enable_if<
			std::is_trivially_copyable<T>::value
		>::type* = nullptr) {
			// Compared to the non-trivially-copyable variant below, just delegate to memmove() and assume it can figure
			// out how to copy from back to front more efficiently that we ever could.
			// Same as with memcpy(), apparently memmove() can't be called with null pointers, even if size is zero. I call that bullying.
			if (count != 0) std::memmove(dst, src, count * sizeof(T));
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
				new(constructDst - 1) T{std::move(*(constructSrc - 1))};
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
			DEATH_DEBUG_ASSERT(index <= arrayGuts.size, arrayGuts.data, "Containers::arrayInsert(): Can't insert at index %zu into an array of size %zu", index, arrayGuts.size);

			// No values to add, early exit
			if (count == 0)
				return arrayGuts.data + index;

			// For arrays with an unknown deleter we'll always move-allocate to a new place, the parts before and after
			// index separately. Not using reallocate() as we don't know where the original memory comes from.
			const std::size_t desiredCapacity = arrayGuts.size + count;
			std::size_t capacity;
#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
			T* oldMid = nullptr;
#endif
			bool needsShiftForward = false;
			if (arrayGuts.deleter != Allocator::deleter) {
				capacity = Allocator::grow(nullptr, desiredCapacity);
				T* const newArray = Allocator::allocate(capacity);
				arrayMoveConstruct<T>(arrayGuts.data, newArray, index);
				arrayMoveConstruct<T>(arrayGuts.data + index, newArray + index + count, arrayGuts.size - index);
				array = Array<T>{newArray, arrayGuts.size, Allocator::deleter};

			// Otherwise, if there's no space anymore, reallocate. which might be able to grow in-place.
			// However we still need to shift the part after index forward.
			} else {
				capacity = Allocator::capacity(arrayGuts.data);
				if (arrayGuts.size + count > capacity) {
					capacity = Allocator::grow(arrayGuts.data, desiredCapacity);
					Allocator::reallocate(arrayGuts.data, arrayGuts.size, capacity);
				} else {
#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
					oldMid = arrayGuts.data + arrayGuts.size;
#endif
				}

				needsShiftForward = true;
			}

			// Increase array size and return the position at index
			T* const it = arrayGuts.data + index;
#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
			__sanitizer_annotate_contiguous_container(
				Allocator::base(arrayGuts.data),
				arrayGuts.data + capacity,
				// For a new allocation, ASan assumes the previous middle pointer is at the end of the array. If we grew an existing
				// allocation, the previous middle is set what __sanitier_acc() received as a middle value before.
				oldMid ? oldMid : arrayGuts.data + capacity,
				arrayGuts.data + arrayGuts.size + count);
#endif

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
		new(it) T{value};
#endif
		return *it;
	}

	template<class T, class Allocator> inline ArrayView<T> arrayInsert(Array<T>& array, std::size_t index, const typename std::common_type<ArrayView<const T>>::type values) {
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

	namespace Implementation
	{
		template<class T> inline void arrayShiftBackward(T* const src, T* const dst, const std::size_t moveCount, std::size_t, typename std::enable_if<
			std::is_trivially_copyable<T>::value
		>::type* = nullptr) {
			// Compared to the non-trivially-copyable variant below, just delegate to memmove() and assume it can figure
			// out how to copy from front to back more efficiently that we ever could.
			// Same as with memcpy(), apparently memmove() can't be called with null pointers, even if size is zero. I call that bullying.
			if (moveCount != 0) std::memmove(dst, src, moveCount * sizeof(T));
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
		DEATH_DEBUG_ASSERT(index + count <= arrayGuts.size, , "Containers::arrayRemove(): Can't remove %zu elements at index %zu from an array of size %zu", count, index, arrayGuts.size);

		// Nothing to remove, yay!
		if (count == 0) return;

		// If we don't have our own deleter, we need to reallocate in order to store the capacity. Move the parts before
		// and after the index separately, which will also cause the removed elements to be properly destructed, so nothing
		// else needs to be done. Not using reallocate() as we don't know where the original memory comes from.
		if (arrayGuts.deleter != Allocator::deleter) {
			T* const newArray = Allocator::allocate(arrayGuts.size - count);
			Implementation::arrayMoveConstruct<T>(arrayGuts.data, newArray, index);
			Implementation::arrayMoveConstruct<T>(arrayGuts.data + index + count, newArray + index, arrayGuts.size - index - count);
			array = Array<T>{newArray, arrayGuts.size - count, Allocator::deleter};

#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
			__sanitizer_annotate_contiguous_container(
				Allocator::base(arrayGuts.data),
				arrayGuts.data + arrayGuts.size,
				arrayGuts.data + arrayGuts.size,
				arrayGuts.data + arrayGuts.size);
#endif

		// Otherwise shift the elements after index backward
		} else {
			Implementation::arrayShiftBackward(arrayGuts.data + index + count, arrayGuts.data + index, arrayGuts.size - index - count, count);
#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
			__sanitizer_annotate_contiguous_container(
				Allocator::base(arrayGuts.data),
				arrayGuts.data + Allocator::capacity(arrayGuts.data),
				arrayGuts.data + arrayGuts.size,
				arrayGuts.data + arrayGuts.size - count);
#endif
			arrayGuts.size -= count;
		}
	}

	template<class T, class Allocator> void arrayRemoveUnordered(Array<T>& array, const std::size_t index, const std::size_t count) {
		// Direct access to speed up debug builds
		auto& arrayGuts = reinterpret_cast<Implementation::ArrayGuts<T>&>(array);
		DEATH_DEBUG_ASSERT(index + count <= arrayGuts.size, , "Containers::arrayRemoveUnordered(): Can't remove %zu elements at index %zu from an array of size %zu", count, index, arrayGuts.size);

		// Nothing to remove, yay!
		if (count == 0) return;

		// If we don't have our own deleter, we need to reallocate in order to store the capacity. Move the parts before
		// and after the index separately, which will also cause the removed elements to be properly destructed, so nothing
		// else needs to be done. Not using reallocate() as we don't know where the original memory comes from.
		if (arrayGuts.deleter != Allocator::deleter) {
			T* const newArray = Allocator::allocate(arrayGuts.size - count);
			Implementation::arrayMoveConstruct<T>(arrayGuts.data, newArray, index);
			Implementation::arrayMoveConstruct<T>(arrayGuts.data + index + count, newArray + index, arrayGuts.size - index - count);
			array = Array<T>{newArray, arrayGuts.size - count, Allocator::deleter};

#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
			__sanitizer_annotate_contiguous_container(
				Allocator::base(arrayGuts.data),
				arrayGuts.data + arrayGuts.size,
				arrayGuts.data + arrayGuts.size,
				arrayGuts.data + arrayGuts.size);
#endif

		// Otherwise move the last count elements over the ones at index, or less if there's not that many after the removed range
		} else {
			const std::size_t moveCount = std::min(count, arrayGuts.size - count - index);
			Implementation::arrayShiftBackward(arrayGuts.data + arrayGuts.size - moveCount, arrayGuts.data + index, moveCount, count);
#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
			__sanitizer_annotate_contiguous_container(
				Allocator::base(arrayGuts.data),
				arrayGuts.data + Allocator::capacity(arrayGuts.data),
				arrayGuts.data + arrayGuts.size,
				arrayGuts.data + arrayGuts.size - count);
#endif
			arrayGuts.size -= count;
		}
	}

	template<class T, class Allocator> void arrayRemoveSuffix(Array<T>& array, const std::size_t count) {
		// Direct access to speed up debug builds
		auto& arrayGuts = reinterpret_cast<Implementation::ArrayGuts<T>&>(array);
		DEATH_DEBUG_ASSERT(count <= arrayGuts.size, , "Containers::arrayRemoveSuffix(): Can't remove %zu elements from an array of size %zu", count, arrayGuts.size);

		// Nothing to remove, yay!
		if (count == 0) return;

		// If we don't have our own deleter, we need to reallocate in order to store the capacity. That'll also cause the excessive
		// elements to be properly destructed, so nothing else needs to be done. Not using reallocate() as we don't know where the original memory comes from.
		if (arrayGuts.deleter != Allocator::deleter) {
			T* const newArray = Allocator::allocate(arrayGuts.size - count);
			Implementation::arrayMoveConstruct<T>(arrayGuts.data, newArray, arrayGuts.size - count);
			array = Array<T>{newArray, arrayGuts.size - count, Allocator::deleter};

#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
			__sanitizer_annotate_contiguous_container(
				Allocator::base(arrayGuts.data),
				arrayGuts.data + arrayGuts.size,
				arrayGuts.data + arrayGuts.size,
				arrayGuts.data + arrayGuts.size);
#endif

		// Otherwise call the destructor on the excessive elements and update the size
		} else {
			Implementation::arrayDestruct<T>(arrayGuts.data + arrayGuts.size - count, arrayGuts.data + arrayGuts.size);
#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
			__sanitizer_annotate_contiguous_container(
				Allocator::base(arrayGuts.data),
				arrayGuts.data + Allocator::capacity(arrayGuts.data),
				arrayGuts.data + arrayGuts.size,
				arrayGuts.data + arrayGuts.size - count);
#endif
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
		Array<T> newArray{NoInit, arrayGuts.size};
		Implementation::arrayMoveConstruct<T>(arrayGuts.data, newArray, arrayGuts.size);
		array = std::move(newArray);

#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
		// Nothing to do (not annotating the arrays with default deleter)
#endif
	}

	template<class T, class Allocator> void arrayShrink(Array<T>& array, DefaultInitT) {
		// Direct access to speed up debug builds
		auto& arrayGuts = reinterpret_cast<Implementation::ArrayGuts<T>&>(array);

		// If not using our growing allocator, assume the array size equals its capacity and do nothing
		if (arrayGuts.deleter != Allocator::deleter)
			return;

		// Even if we don't need to shrink, reallocating to an usual array with common deleters to avoid surprises
		Array<T> newArray{DefaultInit, arrayGuts.size};
		Implementation::arrayMoveAssign<T>(arrayGuts.data, newArray, arrayGuts.size);
		array = std::move(newArray);

#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
		// Nothing to do (not annotating the arrays with default deleter)
#endif
	}
}}

#if defined(_DEATH_CONTAINERS_SANITIZER_ENABLED)
#	undef _DEATH_CONTAINERS_SANITIZER_ENABLED
#endif