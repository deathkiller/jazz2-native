#pragma once

#include "../Common.h"
#include "../Containers/GrowableArray.h"

#include <cstring>

#if defined(DEATH_TARGET_APPLE)
#	include <stdlib.h>
#elif defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS)
#	include <malloc.h>
#endif
#if defined(DEATH_TARGET_MSVC) && !defined(DEATH_TARGET_CLANG_CL)
#	include <intrin.h>	// For _byteswap_ushort()/_byteswap_ulong()/_byteswap_uint64()
#endif

namespace Death { namespace Memory {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

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

		@section Memory-AllocateAligned-initialization Array initialization

		Like with @ref Containers::Array, the returned array is by default
		* *value-initialized*, which means that trivial types are zero-initialized and
		the default constructor is called on other types. Different behavior can be
		achieved with the following tags, compared to @ref Containers::Array the
		initialization is performed separately from the allocation itself with either a
		loop or a call to @ref std::memset().

		-   @ref AllocateAligned(Containers::ValueInitT, std::size_t) is equivalent to the default
			case, zero-initializing trivial types and calling the default constructor
			elsewhere. Useful when you want to make the choice appear explicit.
		-   @ref AllocateAligned(Containers::NoInitT, std::size_t) does not initialize anything.
			Useful for trivial types when you'll be overwriting the contents anyway,
			for non-trivial types this is the dangerous option and you need to call the
			constructor on all elements manually using placement new,
			@ref std::uninitialized_copy() or similar.
	*/
	template<class T, std::size_t alignment = alignof(T)> inline Containers::Array<T> AllocateAligned(std::size_t size);

	/**
		@brief Allocate aligned memory and value-initialize it

		Same as @ref AllocateAligned(std::size_t), just more explicit. Implemented via
		@ref AllocateAligned(Containers::NoInitT, std::size_t) with either a
		@ref std::memset() or a loop calling the constructors on the returned
		allocation.
	*/
	template<class T, std::size_t alignment = alignof(T)> Containers::Array<T> AllocateAligned(Containers::ValueInitT, std::size_t size);

	/**
		@brief Allocate aligned memory and leave it uninitialized

		Compared to @ref AllocateAligned(std::size_t), the memory is left in an
		unitialized state. For non-trivial types, destruction is always done using a
		custom deleter that explicitly calls the destructor on *all elements* --- which
		means that for non-trivial types you're expected to construct all elements
		using placement new (or for example @ref std::uninitialized_copy()) in order to
		avoid calling destructors on uninitialized memory.
	*/
	template<class T, std::size_t alignment = alignof(T)> Containers::Array<T> AllocateAligned(Containers::NoInitT, std::size_t size);

	/**
		@brief Converts a value from/to Big-Endian

		On Little-Endian systems calls @ref SwapBytes(), on Big-Endian systems returns the value unchanged.
		Only trivial types of size 2, 4, or 8 bytes are supported.
	*/
	template<typename T> inline T AsBE(T value);

	/**
		@brief Converts a value from/to Little-Endian

		On Big-Endian systems calls @ref SwapBytes(), on Little-Endian systems returns the value unchanged.
		Only trivial types of size 2, 4, or 8 bytes are supported.
	*/
	template<typename T> inline T AsLE(T value);

	/**
		@brief Returns a value of given size from unaligned pointer
	*/
	template<typename T, typename std::enable_if<std::is_trivially_copyable<T>::value, int>::type = 0>
	inline constexpr T LoadUnaligned(const void* p) noexcept {
		std::remove_const_t<T> v;
		std::memcpy(&v, p, sizeof(T));
		return v;
	}

	/**
		@brief Stores a value of given size to unaligned pointer
	*/
	template<typename T, typename std::enable_if<std::is_trivially_copyable<T>::value, int>::type = 0>
	inline constexpr void StoreUnaligned(void* p, T v) noexcept {
		std::memcpy(p, &v, sizeof(T));
	}

	/**
		@brief Endian-swap bytes of given value

		Only trivial types of size 2, 4, or 8 bytes are supported.
	*/
	template<class T> inline T SwapBytes(T value);

	namespace Implementation
	{
#if defined(DEATH_TARGET_WINDOWS)
		template<class T, typename std::enable_if<std::is_trivially_destructible<T>::value, int>::type = 0> void AlignedDeleter(T* const data, std::size_t) {
			_aligned_free(data);
		}
		template<class T, typename std::enable_if<!std::is_trivially_destructible<T>::value, int>::type = 0> void AlignedDeleter(T* const data, std::size_t size) {
			for (std::size_t i = 0; i != size; ++i) data[i].~T();
			_aligned_free(data);
		}
#else
		template<class T, typename std::enable_if<std::is_trivially_destructible<T>::value, int>::type = 0> void AlignedDeleter(T* const data, std::size_t) {
			std::free(data);
		}
		template<class T, typename std::enable_if<!std::is_trivially_destructible<T>::value, int>::type = 0> void AlignedDeleter(T* const data, std::size_t size) {
			for (std::size_t i = 0; i != size; ++i) data[i].~T();
			std::free(data);
		}
#	if !defined(DEATH_TARGET_UNIX)
		template<class T, typename std::enable_if<std::is_trivially_destructible<T>::value, int>::type = 0> void AlignedOffsetDeleter(T* const data, std::size_t) {
			// Using a unsigned byte in order to be able to represent a 255 byte offset as well
			std::uint8_t* const dataChar = reinterpret_cast<std::uint8_t*>(data);
			std::free(dataChar - *(dataChar - 1));
		}
		template<class T, typename std::enable_if<!std::is_trivially_destructible<T>::value, int>::type = 0> void AlignedOffsetDeleter(T* const data, std::size_t size) {
			for (std::size_t i = 0; i != size; ++i) data[i].~T();

			// Using a unsigned byte in order to be able to represent a 255 byte offset as well
			std::uint8_t* const dataChar = reinterpret_cast<std::uint8_t*>(data);
			std::free(dataChar - dataChar[-1]);
		}
#	endif
#endif

		DEATH_ALWAYS_INLINE std::uint16_t SwapBytes(std::uint16_t x) {
#if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG)
			return __builtin_bswap16(x);
#elif defined(DEATH_TARGET_MSVC)
			return _byteswap_ushort(x);
#elif defined(DEATH_TARGET_APPLE)
			return _OSSwapInt16(value);
#else
			return static_cast<std::uint16_t>((x >> 8) | (x << 8));
#endif
		}

		DEATH_ALWAYS_INLINE std::uint32_t SwapBytes(std::uint32_t x) {
#if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG)
			return __builtin_bswap32(x);
#elif defined(DEATH_TARGET_MSVC)
			return _byteswap_ulong(x);
#elif defined(DEATH_TARGET_APPLE)
			return _OSSwapInt32(value);
#else
			return (x << 24) |
				  ((x & 0x0000FF00u) << 8)  |
				  ((x & 0x00FF0000u) >> 8)  |
				   (x >> 24);
#endif
		}

		DEATH_ALWAYS_INLINE std::uint64_t SwapBytes(std::uint64_t x) {
#if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG)
			return __builtin_bswap64(x);
#elif defined(DEATH_TARGET_MSVC)
			return _byteswap_uint64(x);
#elif defined(DEATH_TARGET_APPLE)
			return _OSSwapInt64(value);
#else
			return (x << 56) |
				  ((x & 0x000000000000FF00ull) << 40) |
				  ((x & 0x0000000000FF0000ull) << 24) |
				  ((x & 0x00000000FF000000ull) << 8)  |
				  ((x & 0x000000FF00000000ull) >> 8)  |
				  ((x & 0x0000FF0000000000ull) >> 24) |
				  ((x & 0x00FF000000000000ull) >> 40) |
				   (x >> 56);
#endif
		}
	}

	template<class T, std::size_t alignment> Containers::Array<T> AllocateAligned(Containers::NoInitT, const std::size_t size) {
		// On non-Unix non-Windows platforms we're storing the alignment offset in a byte right before the returned pointer.
		// Because it's a byte, we can represent a value of at most 255 there (256 would make no sense as a 256-byte-aligned
		// allocation can be only off by 255 bytes at most). Again it's good to have the same requirements on all platforms
		// so checking this always.
		static_assert(alignment && !(alignment & (alignment - 1)) && alignment <= 256,
			"Alignment expected to be a power of two not larger than 256");

		// Required only by aligned_alloc() I think, but it's good to have the same requirements on all platforms for better portability
		DEATH_ASSERT(size * sizeof(T) % alignment == 0, ("Total byte size {} not a multiple of a {}-byte alignment", size * sizeof(T), alignment), {});

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
		int result = posix_memalign(&data, alignment < sizeof(void*) ? sizeof(void*) : alignment, size * sizeof(T));
		DEATH_DEBUG_ASSERT(result == 0);
		return Containers::Array<T>{static_cast<T*>(data), size, Implementation::AlignedDeleter<T>};
#elif defined(DEATH_TARGET_WINDOWS)
		// Windows platforms
		// Zero size is not allowed: https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/aligned-malloc
		if (!size) return {};

		return Containers::Array<T>{static_cast<T*>(_aligned_malloc(size * sizeof(T), alignment)), size, Implementation::AlignedDeleter<T>};
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
			deleter = Implementation::AlignedDeleter<T>;
		} else {
			(pointer + offset)[-1] = offset;
			deleter = Implementation::AlignedOffsetDeleter<T>;
		}
		return Containers::Array<T>{reinterpret_cast<T*>(pointer + offset), size, deleter};
#endif
	}

	template<class T, std::size_t alignment> Containers::Array<T> AllocateAligned(Containers::ValueInitT, const std::size_t size) {
		Containers::Array<T> out = AllocateAligned<T, alignment>(Containers::NoInit, size);
		Containers::Implementation::arrayConstruct(Containers::ValueInit, out.begin(), out.end());
		return out;
	}

	template<class T, std::size_t alignment> inline Containers::Array<T> AllocateAligned(std::size_t size) {
		return AllocateAligned<T, alignment>(Containers::ValueInit, size);
	}

	template<typename T> inline T AsBE(T value) {
#if !defined(DEATH_TARGET_BIG_ENDIAN)
		return SwapBytes(value);
#else
		return value;
#endif
	}

	template<typename T> inline T AsLE(T value) {
#if defined(DEATH_TARGET_BIG_ENDIAN)
		return SwapBytes(value);
#else
		return value;
#endif
	}

	template<class T> inline T SwapBytes(T value) {
		static_assert(std::is_trivially_copyable<T>::value, "SwapBytes() requires the source type to be trivially copyable");

		typedef typename Death::Implementation::TypeFor<sizeof(T)>::Type U;

		// std::memcpy() is used instead of std::bit_cast() for compatibility with C++11
		U tmp;
		std::memcpy(&tmp, &value, sizeof(U));
		tmp = Implementation::SwapBytes(tmp);
		std::memcpy(&value, &tmp, sizeof(U));
		return value;
	}

}}