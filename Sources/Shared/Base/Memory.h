#pragma once

#include "../Common.h"
#include "../Containers/GrowableArray.h"

#if defined(DEATH_TARGET_APPLE)
#	include <stdlib.h>
#elif defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS)
#	include <malloc.h>
#endif

namespace Death {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	class Memory
	{
	public:
		Memory() = delete;
		~Memory() = delete;

		/**
			@brief Allocate aligned memory and value-initialize it
			@tparam T           Type of the returned array
			@tparam alignment   Allocation alignment, in bytes
			@param  size        Count of @p T items to allocate. If @cpp 0 @ce, no allocation is done.

			Compared to the classic C @ref std::malloc() or C++ @cpp new @ce that commonly
			aligns only to @cpp 2*sizeof(void*) @ce, this function returns "overaligned"
			allocations, which is mainly useful for efficient SIMD operations.

			The alignment is implicitly @cpp alignof(T) @ce, but can be overridden with the
			@p alignment template parameter. When specified explicitly, it is expected to
			be a power-of-two value, at most @cpp 256 @ce bytes and the total byte size
			being a multiple of the alignment.

			The returned pointer is always aligned to at least the desired value, but the
			alignment can be also higher. For example, allocating a 2 MB buffer may result
			in it being aligned to the whole memory page, or small alignment values could
			get rounded up to the default @cpp 2*sizeof(void*) @ce alignment.

			@section Utility-allocateAligned-initialization Array initialization

			Like with @ref Containers::Array, the returned array is by default
			* *value-initialized*, which means that trivial types are zero-initialized and
			the default constructor is called on other types. Different behavior can be
			achieved with the following tags, compared to @ref Containers::Array the
			initialization is performed separately from the allocation itself with either a
			loop or a call to @ref std::memset().

			-   @ref allocateAligned(DefaultInitT, std::size_t) leaves trivial types
				uninitialized and calls the default constructor elsewhere. Because of the
				differing behavior for trivial types it's better to explicitly use either
				the @ref ValueInit or @ref NoInit variants instead.
			-   @ref allocateAligned(ValueInitT, std::size_t) is equivalent to the default
				case, zero-initializing trivial types and calling the default constructor
				elsewhere. Useful when you want to make the choice appear explicit.
			-   @ref allocateAligned(NoInitT, std::size_t) does not initialize anything.
				Useful for trivial types when you'll be overwriting the contents anyway,
				for non-trivial types this is the dangerous option and you need to call the
				constructor on all elements manually using placement new,
				@ref std::uninitialized_copy() or similar --- see the function docs for an
				example.
		*/
		template<class T, std::size_t alignment = alignof(T)> inline static Containers::Array<T> allocateAligned(std::size_t size);

		/**
			@brief Allocate aligned memory and default-initialize it

			Compared to @ref allocateAligned(std::size_t), trivial types are not
			initialized and default constructor is called otherwise. Because of the
			differing behavior for trivial types it's better to explicitly use either the
			@ref allocateAligned(ValueInitT, std::size_t) or the
			@ref allocateAligned(NoInitT, std::size_t) variant instead.

			Implemented via @ref allocateAligned(NoInitT, std::size_t) with a
			loop calling the constructors on the returned allocation in case of non-trivial
			types.
		*/
		template<class T, std::size_t alignment = alignof(T)> static Containers::Array<T> allocateAligned(Containers::DefaultInitT, std::size_t size);

		/**
			@brief Allocate aligned memory and value-initialize it

			Same as @ref allocateAligned(std::size_t), just more explicit. Implemented via
			@ref allocateAligned(NoInitT, std::size_t) with either a
			@ref std::memset() or a loop calling the constructors on the returned
			allocation.
		*/
		template<class T, std::size_t alignment = alignof(T)> static Containers::Array<T> allocateAligned(Containers::ValueInitT, std::size_t size);

		/**
			@brief Allocate aligned memory and leave it uninitialized
			@m_since_latest

			Compared to @ref allocateAligned(std::size_t), the memory is left in an
			unitialized state. For trivial types is equivalent to
			@ref allocateAligned(DefaultInitT, std::size_t). For non-trivial
			types, destruction is always done using a custom deleter that explicitly calls
			the destructor on *all elements* --- which means that for non-trivial types
			you're expected to construct all elements using placement new (or for example
			@ref std::uninitialized_copy()) in order to avoid calling destructors on
			uninitialized memory.
		*/
		template<class T, std::size_t alignment = alignof(T)> static Containers::Array<T> allocateAligned(Containers::NoInitT, std::size_t size);

		/**
			@brief Returns a value of given size from unaligned pointer
		*/
		template<typename T, typename std::enable_if<std::is_trivially_copyable<T>::value, int>::type = 0>
		inline static constexpr T loadUnaligned(const void* p) noexcept {
			std::remove_const_t<T> v;
			std::memcpy(&v, p, sizeof(T));
			return v;
		}

		/**
			@brief Stores a value of given size to unaligned pointer
		*/
		template<typename T, typename std::enable_if<std::is_trivially_copyable<T>::value, int>::type = 0>
		inline static constexpr void storeUnaligned(void* p, T v) noexcept {
			std::memcpy(p, &v, sizeof(T));
		}
	};

	namespace Implementation
	{
#if defined(DEATH_TARGET_WINDOWS)
		template<class T, typename std::enable_if<std::is_trivially_destructible<T>::value, int>::type = 0> void alignedDeleter(T* const data, std::size_t) {
			_aligned_free(data);
		}
		template<class T, typename std::enable_if<!std::is_trivially_destructible<T>::value, int>::type = 0> void alignedDeleter(T* const data, std::size_t size) {
			for (std::size_t i = 0; i != size; ++i) data[i].~T();
			_aligned_free(data);
		}
#else
		template<class T, typename std::enable_if<std::is_trivially_destructible<T>::value, int>::type = 0> void alignedDeleter(T* const data, std::size_t) {
			std::free(data);
		}
		template<class T, typename std::enable_if<!std::is_trivially_destructible<T>::value, int>::type = 0> void alignedDeleter(T* const data, std::size_t size) {
			for (std::size_t i = 0; i != size; ++i) data[i].~T();
			std::free(data);
		}
#	if !defined(DEATH_TARGET_UNIX)
		template<class T, typename std::enable_if<std::is_trivially_destructible<T>::value, int>::type = 0> void alignedOffsetDeleter(T* const data, std::size_t) {
			// Using a unsigned byte in order to be able to represent a 255 byte offset as well
			std::uint8_t* const dataChar = reinterpret_cast<std::uint8_t*>(data);
			std::free(dataChar - *(dataChar - 1));
		}
		template<class T, typename std::enable_if<!std::is_trivially_destructible<T>::value, int>::type = 0> void alignedOffsetDeleter(T* const data, std::size_t size) {
			for (std::size_t i = 0; i != size; ++i) data[i].~T();

			// Using a unsigned byte in order to be able to represent a 255 byte offset as well
			std::uint8_t* const dataChar = reinterpret_cast<std::uint8_t*>(data);
			std::free(dataChar - dataChar[-1]);
		}
#	endif
#endif
	}

	template<class T, std::size_t alignment> Containers::Array<T> Memory::allocateAligned(Containers::NoInitT, const std::size_t size) {
		// On non-Unix non-Windows platforms we're storing the alignment offset in a byte right before the returned pointer.
		// Because it's a byte, we can represent a value of at most 255 there (256 would make no sense as a 256-byte-aligned
		// allocation can be only off by 255 bytes at most). Again it's good to have the same requirements on all platforms
		// so checking this always.
		static_assert(alignment && !(alignment & (alignment - 1)) && alignment <= 256,
			"Alignment expected to be a power of two not larger than 256");

		// Required only by aligned_alloc() I think, but it's good to have the same requirements on all platforms for better portability
		DEATH_ASSERT(size * sizeof(T) % alignment == 0, ("Total byte size %zu not a multiple of a %zu-byte alignment", size * sizeof(T), alignment), {});

#if defined(DEATH_TARGET_UNIX)
		// Unix platforms
		// For some reason, allocating zero bytes still returns a non-null pointer which seems weird and confusing. Handle that explicitly instead.
		if (!size) return {};

		// I would use aligned_alloc() but then there's APPLE who comes and says NO. And on top of everything
		// they DARE to have posix_memalign() in a different header.
		// What's perhaps a bit surprising is that posix_memalign() requires the alignment to be >= sizeof(void*).
		// It seems like a strange requirement -- it could just overalign for lower alignment values instead of failing.
		// Which is what we do here. The Windows _aligned_malloc() API doesn't have this requirement.
		void* data = {};
		int result = posix_memalign(&data, std::max(alignment, sizeof(void*)), size * sizeof(T));
		DEATH_DEBUG_ASSERT(result == 0);
		return Containers::Array<T>{static_cast<T*>(data), size, Implementation::alignedDeleter<T>};
#elif defined(DEATH_TARGET_WINDOWS)
		// Windows
		// Zero size is not allowed: https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-malloc
		if (!size) return {};

		return Containers::Array<T>{static_cast<T*>(_aligned_malloc(size * sizeof(T), alignment)), size, Implementation::alignedDeleter<T>};
#else
		// Other -- for allocations larger than the min alignment allocate with (align - 1) more and align manually,
		// then provide a custom deleter that undoes this.
		// Because we always allocate `alignment - 1` more than the size, it means even zero-size allocations would be allocations.
		if (!size) return {};

		// Using a unsigned byte in order to be able to represent a 255 byte offset as well
		std::uint8_t* pointer;
		std::ptrdiff_t offset;
		if (alignment <= Containers::Implementation::DefaultAllocationAlignment) {
			pointer = static_cast<std::uint8_t*>(std::malloc(size * sizeof(T)));
			offset = 0;
		} else {
			pointer = static_cast<std::uint8_t*>(std::malloc(size * sizeof(T) + alignment - 1));
			if (reinterpret_cast<std::ptrdiff_t>(pointer) % alignment == 0) {
				offset = 0;
			} else {
				offset = alignment - reinterpret_cast<std::ptrdiff_t>(pointer) % alignment;
			}
		}
		DEATH_DEBUG_ASSERT((reinterpret_cast<std::ptrdiff_t>(pointer) + offset) % alignment == 0);

		// If the offset is zero, use the classic std::free() directly. If not, save the offset in the byte right
		// before what the output pointer will point to and use a different deleter that will undo this offset
		// before calling std::free().
		void(*deleter)(T*, std::size_t);
		if (offset == 0) {
			deleter = Implementation::alignedDeleter<T>;
		} else {
			(pointer + offset)[-1] = offset;
			deleter = Implementation::alignedOffsetDeleter<T>;
		}
		return Containers::Array<T>{reinterpret_cast<T*>(pointer + offset), size, deleter};
#endif
	}

	template<class T, std::size_t alignment> Containers::Array<T> Memory::allocateAligned(Containers::DefaultInitT, const std::size_t size) {
		Containers::Array<T> out = allocateAligned<T, alignment>(Containers::NoInit, size);
		Containers::Implementation::arrayConstruct(Containers::DefaultInit, out.begin(), out.end());
		return out;
	}

	template<class T, std::size_t alignment> Containers::Array<T> Memory::allocateAligned(Containers::ValueInitT, const std::size_t size) {
		Containers::Array<T> out = allocateAligned<T, alignment>(Containers::NoInit, size);
		Containers::Implementation::arrayConstruct(Containers::ValueInit, out.begin(), out.end());
		return out;
	}

	template<class T, std::size_t alignment> inline Containers::Array<T> Memory::allocateAligned(std::size_t size) {
		return allocateAligned<T, alignment>(Containers::ValueInit, size);
	}

}