#include "SmallVector.h"

#if defined(__cpp_exceptions) && !defined(DEATH_SUPPRESS_EXCEPTIONS)
#	include <stdexcept>
#endif

namespace Death { namespace Containers {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	// Check that no bytes are wasted and everything is well-aligned
	namespace
	{
		// These structures may cause binary compat warnings on AIX. Suppress the
		// warning since we are only using these types for the static assertions below.
#if defined(_AIX)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waix-compat"
#endif
		struct Struct16B {
			alignas(16) void* X;
		};
		struct Struct32B {
			alignas(32) void* X;
		};
#if defined(_AIX)
#pragma GCC diagnostic pop
#endif
	}

	static_assert(sizeof(SmallVector<void*, 0>) == sizeof(unsigned) * 2 + sizeof(void*),
				  "Wasted space in Containers::SmallVector size 0");
	static_assert(alignof(SmallVector<Struct16B, 0>) >= alignof(Struct16B),
				  "Wrong alignment for 16-byte aligned T");
	static_assert(alignof(SmallVector<Struct32B, 0>) >= alignof(Struct32B),
				  "Wrong alignment for 32-byte aligned T");
	static_assert(sizeof(SmallVector<Struct16B, 0>) >= alignof(Struct16B),
				  "Missing padding for 16-byte aligned T");
	static_assert(sizeof(SmallVector<Struct32B, 0>) >= alignof(Struct32B),
				  "Missing padding for 32-byte aligned T");
	static_assert(sizeof(SmallVector<void*, 1>) == sizeof(unsigned) * 2 + sizeof(void*) * 2,
				  "Wasted space in Containers::SmallVector size 1");

	static_assert(sizeof(SmallVector<char, 0>) == sizeof(void*) * 2 + sizeof(void*),
				  "1 byte elements have word-sized type for size and capacity");

	// Reports that MinSize doesn't fit into this vector's size type
	static void reportSizeOverflow(std::size_t minSize, std::size_t maxSize) {
		//std::string reason = "SmallVector unable to grow. Requested capacity (" +
		//	std::to_string(minSize) +
		//	") is larger than maximum value for size type (" +
		//	std::to_string(maxSize) + ")";
		//throw std::length_error(reason);

#if defined(__cpp_exceptions) && !defined(DEATH_SUPPRESS_EXCEPTIONS)
		throw std::length_error("Containers::SmallVector capacity unable to grow");
#endif
	}

	// Reports that this vector is already at maximum capacity
	static void reportAtMaximumCapacity(std::size_t maxSize) {
		//std::string reason =
		//	"SmallVector capacity unable to grow. Already at maximum size " +
		//	std::to_string(maxSize);
		//throw std::length_error(reason);

#if defined(__cpp_exceptions) && !defined(DEATH_SUPPRESS_EXCEPTIONS)
		throw std::length_error("Containers::SmallVector capacity already at maximum size");
#endif
	}

	// Note: Moving this function into the header may cause performance regression.
	template<class Size_T>
	static std::size_t getNewCapacity(std::size_t minSize, std::size_t typeSize, std::size_t oldCapacity) {
		constexpr std::size_t maxSize = std::numeric_limits<Size_T>::max();

		// Ensure we can fit the new capacity.
		// This is only going to be applicable when the capacity is 32 bit.
		if (minSize > maxSize) {
			reportSizeOverflow(minSize, maxSize);
		}

		// Ensure we can meet the guarantee of space for at least one more element.
		// The above check alone will not catch the case where grow is called with a
		// default MinSize of 0, but the current capacity cannot be increased.
		// This is only going to be applicable when the capacity is 32 bit.
		if (oldCapacity == maxSize) {
			reportAtMaximumCapacity(maxSize);
		}

		// In theory 2*capacity can overflow if the capacity is 64 bit, but the
		// original capacity would never be large enough for this to be a problem.
		std::size_t newCapacity = 2 * oldCapacity + 1; // Always grow.
		return std::min(std::max(newCapacity, minSize), maxSize);
	}

	static void* replaceAllocation(void* newElts, std::size_t typeSize, std::size_t newCapacity, std::size_t vSize = 0) {
		void* newEltsReplace = std::malloc(newCapacity * typeSize);
		if (vSize != 0) {
			std::memcpy(newEltsReplace, newElts, vSize * typeSize);
		}
		std::free(newElts);
		return newEltsReplace;
	}

	// Note: Moving this function into the header may cause performance regression.
	template<class Size_T>
	void* SmallVectorBase<Size_T>::mallocForGrow(void* firstEl, std::size_t minSize, std::size_t typeSize, std::size_t& newCapacity) {
		newCapacity = getNewCapacity<Size_T>(minSize, typeSize, this->capacity());
		// Even if capacity is not 0 now, if the vector was originally created with
		// capacity 0, it's possible for the malloc to return FirstEl.
		void* newElts = std::malloc(newCapacity * typeSize);
		if (newElts == firstEl) {
			newElts = replaceAllocation(newElts, typeSize, newCapacity);
		}
		return newElts;
	}

	// Note: Moving this function into the header may cause performance regression.
	template<class Size_T>
	void* SmallVectorBase<Size_T>::mallocForShrink(void* firstEl, std::size_t newCapacity, std::size_t typeSize) {
		return std::realloc(firstEl, newCapacity * typeSize);
	}

	// Note: Moving this function into the header may cause performance regression.
	template<class Size_T>
	void SmallVectorBase<Size_T>::growTrivial(void* firstEl, std::size_t minSize, std::size_t typeSize) {
		std::size_t newCapacity = getNewCapacity<Size_T>(minSize, typeSize, this->capacity());
		void* newElts;
		if (BeginX == firstEl) {
			newElts = std::malloc(newCapacity * typeSize);
			if (newElts == firstEl) {
				newElts = replaceAllocation(newElts, typeSize, newCapacity);
			}
			// Copy the elements over. No need to run dtors on PODs.
			std::memcpy(newElts, this->BeginX, size() * typeSize);
		} else {
			// If this wasn't grown from the inline copy, grow the allocated space.
			newElts = std::realloc(this->BeginX, newCapacity * typeSize);
			if (newElts == firstEl) {
				newElts = replaceAllocation(newElts, typeSize, newCapacity, size());
			}
		}

		this->setAllocationRange(newElts, newCapacity);
	}

	template class SmallVectorBase<uint32_t>;

	// Disable the uint64_t instantiation for 32-bit builds.
	// Both uint32_t and uint64_t instantiations are needed for 64-bit builds.
	// This instantiation will never be used in 32-bit builds, and will cause
	// warnings when sizeof(Size_T) > sizeof(std::size_t).
#if SIZE_MAX > UINT32_MAX
	template class SmallVectorBase<std::uint64_t>;

	// Assertions to ensure this #if stays in sync with SmallVectorSizeType.
	static_assert(sizeof(SmallVectorSizeType<char>) == sizeof(std::uint64_t),
				  "Expected Containers::SmallVectorBase<std::uint64_t> variant to be in use");
#else
	static_assert(sizeof(SmallVectorSizeType<char>) == sizeof(std::uint32_t),
				  "Expected Containers::SmallVectorBase<std::uint32_t> variant to be in use");
#endif

}}