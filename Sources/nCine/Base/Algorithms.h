#pragma once

#include "Iterator.h"
#include "pdqsort/pdqsort.h"

#include <algorithm>
#include <cmath>

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death;

namespace nCine
{
	// Traits
	template<class T>
	struct isIntegral
	{
		static constexpr bool value = false;
	};
	template<>
	struct isIntegral<bool>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<char>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<unsigned char>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<short int>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<unsigned short int>
	{
		static constexpr bool value = true;
	};
	template <>
	struct isIntegral<int>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<unsigned int>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<long>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<unsigned long>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<long long>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<unsigned long long>
	{
		static constexpr bool value = true;
	};

	template<class T>
	inline bool IsLess(const T &a, const T &b)
	{
		return a < b;
	}
	
	template<class T>
	inline bool IsNotLess(const T &a, const T &b)
	{
		return !(a < b);
	}

	/// Returns true if the range is sorted into ascending order
	template<class Iterator>
	inline bool isSorted(Iterator first, const Iterator last)
	{
		if (first == last)
			return true;

		Iterator next = first;
		while (++next != last) {
			if (*next < *first)
				return false;
			++first;
		}

		return true;
	}

	/// Returns true if the range is sorted, using a custom comparison
	template<class Iterator, class Compare>
	inline bool isSorted(Iterator first, const Iterator last, Compare comp)
	{
		if (first == last)
			return true;

		Iterator next = first;
		while (++next != last) {
			if (comp(*next, *first))
				return false;
			++first;
		}

		return true;
	}

	/// Returns an iterator to the first element in the range which does not follow an ascending order, or last if sorted
	template<class Iterator>
	inline const Iterator isSortedUntil(Iterator first, const Iterator last)
	{
		if (first == last)
			return first;

		Iterator next = first;
		while (++next != last) {
			if (*next < *first)
				return next;
			++first;
		}

		return last;
	}

	/// Returns an iterator to the first element in the range which does not follow the custom comparison, or last if sorted
	template<class Iterator, class Compare>
	inline const Iterator isSortedUntil(Iterator first, const Iterator last, Compare comp)
	{
		if (first == last)
			return first;

		Iterator next = first;
		while (++next != last) {
			if (comp(*next, *first))
				return next;
			++first;
		}

		return last;
	}

	/// A container for functions to destruct objects and arrays of objects
	template<bool value>
	struct destructHelpers
	{
		template<class T>
		inline static void destructObject(T* ptr)
		{
			ptr->~T();
		}

		template<class T>
		inline static void destructArray(T* ptr, std::uint32_t numElements)
		{
			for (std::uint32_t i = 0; i < numElements; i++)
				ptr[numElements - i - 1].~T();
		}
	};

	namespace detail
	{
		template<class T>
		struct typeIdentity
		{
			using type = T;
		};

		template<class T>
		auto tryAddRValueReference(int)->typeIdentity<T&&>;
		template<class T>
		auto tryAddRValueReference(...)->typeIdentity<T>;
	}

	template<class T>
	struct addRValueReference : decltype(detail::tryAddRValueReference<T>(0)) {};
	template<class T>
	typename addRValueReference<T>::type declVal();

	/// Specialization for trivially destructible types
	template<class T, typename = void>
	struct isDestructible
	{
		static constexpr bool value = false;
	};
	template<class T>
	struct isDestructible<T, decltype(declVal<T&>().~T())>
	{
		static constexpr bool value = (true && !__is_union(T));
	};

	// Use `__has_trivial_destructor()` only on GCC
#if defined(__GNUC__) && !defined(__clang__) && !defined(__INTEL_COMPILER)
	template<class T>
	struct hasTrivialDestructor
	{
		static constexpr bool value = __has_trivial_destructor(T);
	};

	template<class T>
	struct isTriviallyDestructible
	{
		static constexpr bool value = isDestructible<T>::value && hasTrivialDestructor<T>::value;
	};
#else
	template<class T>
	struct isTriviallyDestructible
	{
		static constexpr bool value = __is_trivially_destructible(T);
	};
#endif

	template<>
	struct destructHelpers<true>
	{
		template<class T>
		inline static void destructObject(T* ptr)
		{
		}

		template<class T>
		inline static void destructArray(T* ptr, unsigned int numElements)
		{
		}
	};

	template<class T>
	void destructObject(T* ptr)
	{
		destructHelpers<isTriviallyDestructible<T>::value>::destructObject(ptr);
	}

	template<class T>
	void destructArray(T* ptr, std::uint32_t numElements)
	{
		destructHelpers<isTriviallyDestructible<T>::value>::destructArray(ptr, numElements);
	}

	inline float lerp(float a, float b, float ratio)
	{
		return a + ratio * (b - a);
	}

	inline std::int32_t lerp(std::int32_t a, std::int32_t b, float ratio)
	{
		return (std::int32_t)std::round(a + ratio * (float)(b - a));
	}

	inline float lerpByTime(float a, float b, float ratio, float timeMult)
	{
		float normalizedRatio = 1.0f - powf(1.0f - ratio, timeMult);
		return a + normalizedRatio * (b - a);
	}

	std::int32_t copyStringFirst(char* dest, std::int32_t destSize, const char* source, std::int32_t count = -1);

	template <std::size_t size>
	inline std::int32_t copyStringFirst(char(&dest)[size], const char* source, std::int32_t count = -1) {
		return copyStringFirst(dest, size, source, count);
	}

	int formatString(char* buffer, std::size_t maxLen, const char* format, ...);

	void u32tos(std::uint32_t value, char* buffer);
	void i32tos(std::int32_t value, char* buffer);
	void u64tos(std::uint64_t value, char* buffer);
	void i64tos(std::int64_t value, char* buffer);
	void ftos(double value, char* buffer, std::int32_t bufferSize);

	constexpr bool isDigit(char c)
	{
		return (c >= '0' && c <= '9');
	}

	constexpr std::uint32_t stou32(const char* str, std::size_t length)
	{
		std::uint32_t n = 0;
		while (length > 0) {
			if (!isDigit(*str)) {
				break;
			}
			n *= 10;
			n += (*str++ - '0');
			length--;
		}
		return n;
	}

	constexpr std::uint64_t stou64(const char* str, std::size_t length)
	{
		std::uint64_t n = 0;
		while (length > 0) {
			if (!isDigit(*str)) {
				break;
			}
			n *= 10;
			n += (*str++ - '0');
			length--;
		}
		return n;
	}

	template<class Iter, class Compare>
	inline void sort(Iter begin, Iter end, Compare comp)
	{
#if defined(PREFER_STD_SORT)
		std::sort(begin, end, comp);
#else
		pdqsort(begin, end, comp);
#endif
	}

	template<class Iter>
	inline void sort(Iter begin, Iter end)
	{
#if defined(PREFER_STD_SORT)
		std::sort(begin, end);
#else
		pdqsort(begin, end);
#endif
	}

	float halfToFloat(std::uint16_t value);
	std::uint16_t floatToHalf(float value);

	constexpr std::uint64_t parseVersion(const Containers::StringView& version)
	{
		std::size_t versionLength = version.size();
		std::size_t dotIndices[3] { };
		std::size_t foundCount = 0;

		for (std::size_t i = 0; i < versionLength; i++) {
			if (version[i] == '.') {
				dotIndices[foundCount++] = i;
				if (foundCount >= countof(dotIndices) - 1) {
					// Save only indices of the first 2 dots and keep the last index for string length
					break;
				}
			}
		}

		dotIndices[foundCount] = versionLength;

		std::uint64_t major = stou32(&version[0], dotIndices[0]);
		std::uint64_t minor = (foundCount >= 1 ? stou32(&version[dotIndices[0] + 1], dotIndices[1] - dotIndices[0] - 1) : 0);
		std::uint64_t patch = (foundCount >= 2
			? (version[dotIndices[1] + 1] != 'r'
				? stou32(&version[dotIndices[1] + 1], dotIndices[2] - dotIndices[1] - 1)
				: 0x0FFFFFFFULL) // GIT Revision - use special value, so it's always the latest (without upper 4 bits)
			: 0);

		return (patch & 0xFFFFFFFFULL) | ((minor & 0xFFFFULL) << 32) | ((major & 0xFFFFULL) << 48);
	}
}
