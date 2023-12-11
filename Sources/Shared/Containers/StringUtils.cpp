#include "StringUtils.h"
#include "GrowableArray.h"
#include "../Cpu.h"

#if defined(DEATH_ENABLE_SSE2)
#	include "../IntrinsicsSse2.h"
#endif
#if defined(DEATH_ENABLE_AVX2)
#	include "../IntrinsicsAvx.h"
#endif
#if defined(DEATH_ENABLE_SIMD128)
#	include <wasm_simd128.h>
#endif

namespace Death::Containers::StringUtils
{
	namespace Implementation
	{
		namespace
		{
			DEATH_CPU_MAYBE_UNUSED typename std::decay<decltype(lowercaseInPlace)>::type lowercaseInPlaceImplementation(Cpu::ScalarT) {
				return [](char* data, const std::size_t size) {
					// A proper Unicode-aware *and* locale-aware solution would involve far more than iterating over bytes

					// Branchless idea from https://stackoverflow.com/a/3884737, what it does is adding (1 << 5) for 'A'
					// and all 26 letters after, and (0 << 5) for anything after (and before as well, which is what
					// the unsigned cast does). The (1 << 5) bit (0x20) is what differs between lowercase and uppercase
					// characters. See Test/StringBenchmark.cpp for other alternative implementations leading up to this
					// point. In particular, the std::uint8_t() is crucial, unsigned() is 6x to 8x slower.
					const char* const end = data + size;
					for (char* c = data; c != end; ++c) {
						*c += (std::uint8_t(*c - 'A') < 26) << 5;
					}
				};
			}

			DEATH_CPU_MAYBE_UNUSED typename std::decay<decltype(uppercaseInPlace)>::type uppercaseInPlaceImplementation(Cpu::ScalarT) {
				return [](char* data, const std::size_t size) {
					// Same as above, except that (1 << 5) is subtracted for 'a' and all 26 letters after.
					const char* const end = data + size;
					for (char* c = data; c != end; ++c) {
						*c -= (std::uint8_t(*c - 'a') < 26) << 5;
					}
				};
			}

#if defined(DEATH_ENABLE_SSE2)
			// The core vector algorithm was reverse-engineered from what GCC (and apparently also Clang) does for the scalar
			// case with SSE2 optimizations enabled. It's the same count of instructions as the "obvious" case of doing two
			// comparisons per character, ORing that, and then applying a bitmask, but considerably faster.
			DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_SSE2 typename std::decay<decltype(lowercaseInPlace)>::type lowercaseInPlaceImplementation(Cpu::Sse2T) {
				return [](char* const data, const std::size_t size) DEATH_ENABLE_SSE2 {
					char* const end = data + size;

					// If we have less than 16 bytes, do it the stupid way, equivalent to the scalar variant and just unrolled.
					{
						char* j = data;
						switch(size) {
							case 15: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 14: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 13: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 12: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 11: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 10: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  9: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  8: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  7: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  6: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  5: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  4: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  3: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  2: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  1: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  0: return;
						}
					}

					// Core algorithm
					const __m128i aAndAbove = _mm_set1_epi8(char(256u - std::uint8_t('A')));
					const __m128i lowest25 = _mm_set1_epi8(25);
					const __m128i lowercaseBit = _mm_set1_epi8(0x20);
					const auto lowercaseOneVector = [&](const __m128i chars) DEATH_ENABLE_SSE2 {
						// Moves 'A' and everything above to 0 and up (it overflows and wraps around)
						const __m128i uppercaseInLowest25 = _mm_add_epi8(chars, aAndAbove);
						// Subtracts 25 with saturation, which makes the original 'A' to 'Z' (now 0 to 25) zero and everything else non-zero
						const __m128i lowest25IsZero = _mm_subs_epu8(uppercaseInLowest25, lowest25);
						// Mask indicating where uppercase letters where, i.e. which values are now zero
						const __m128i maskUppercase = _mm_cmpeq_epi8(lowest25IsZero, _mm_setzero_si128());
						// For the masked chars a lowercase bit is set, and the bit is then added to the original chars,
						// making the uppercase chars lowercase
						return _mm_add_epi8(chars, _mm_and_si128(maskUppercase, lowercaseBit));
					};

					// Unconditionally convert the first vector in a slower, unaligned way. Any extra branching to avoid the unaligned
					// load & store if already aligned would be most probably more expensive than the actual operation.
					{
						const __m128i chars = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data));
						_mm_storeu_si128(reinterpret_cast<__m128i*>(data), lowercaseOneVector(chars));
					}

					// Go to the next aligned position. If the pointer was already aligned, we'll go to the next aligned vector;
					// if not, there will be an overlap and we'll convert some bytes twice. Which is fine,
					// lowercasing already-lowercased data is a no-op.
					char* i = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(data + 16) & ~0xf);

					// Convert all aligned vectors using aligned load/store
					for(; i + 16 <= end; i += 16) {
						const __m128i chars = _mm_load_si128(reinterpret_cast<const __m128i*>(i));
						_mm_store_si128(reinterpret_cast<__m128i*>(i), lowercaseOneVector(chars));
					}

					// Handle remaining less than a vector with an unaligned load & store, again overlapping back
					// with the previous already-converted elements
					if(i < end) {
						i = end - 16;
						const __m128i chars = _mm_loadu_si128(reinterpret_cast<const __m128i*>(i));
						_mm_storeu_si128(reinterpret_cast<__m128i*>(i), lowercaseOneVector(chars));
					}
				};
			}

			// Compared to the lowercase implementation it (obviously) uses the scalar uppercasing code in the less-than-16 case.
			// In the vector case zeroes out the a-z range instead of A-Z, and subtracts the lowercase bit instead of adding.
			DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_SSE2 typename std::decay<decltype(uppercaseInPlace)>::type uppercaseInPlaceImplementation(Cpu::Sse2T) {
				return [](char* const data, const std::size_t size) DEATH_ENABLE_SSE2 {
					char* const end = data + size;

					// If we have less than 16 bytes, do it the stupid way, equivalent to the scalar variant and just unrolled.
					{
						char* j = data;
						switch(size) {
							case 15: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 14: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 13: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 12: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 11: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 10: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  9: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  8: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  7: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  6: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  5: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  4: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  3: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  2: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  1: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  0: return;
						}
					}

					// Core algorithm
					const __m128i aAndAbove = _mm_set1_epi8(char(256u - std::uint8_t('a')));
					const __m128i lowest25 = _mm_set1_epi8(25);
					const __m128i lowercaseBit = _mm_set1_epi8(0x20);
					const auto uppercaseOneVector = [&](const __m128i chars) DEATH_ENABLE_SSE2 {
						// Moves 'a' and everything above to 0 and up (it overflows and wraps around)
						const __m128i lowercaseInLowest25 = _mm_add_epi8(chars, aAndAbove);
						// Subtracts 25 with saturation, which makes the original 'a' to 'z' (now 0 to 25) zero and everything else non-zero
						const __m128i lowest25IsZero = _mm_subs_epu8(lowercaseInLowest25, lowest25);
						// Mask indicating where uppercase letters where, i.e. which values arenow zero
						const __m128i maskUppercase = _mm_cmpeq_epi8(lowest25IsZero, _mm_setzero_si128());
						// For the masked chars a lowercase bit is set, and the bit is then subtracted from the original chars,
						// making the lowercase chars uppercase
						return _mm_sub_epi8(chars, _mm_and_si128(maskUppercase, lowercaseBit));
					};

					// Unconditionally convert the first vector in a slower, unaligned way. Any extra branching to avoid the unaligned
					// load & store if already aligned would be most probably more expensive than the actual operation.
					{
						const __m128i chars = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data));
						_mm_storeu_si128(reinterpret_cast<__m128i*>(data), uppercaseOneVector(chars));
					}

					// Go to the next aligned position. If the pointer was already aligned, we'll go to the next aligned
					// vector; if not, there will be an overlap and we'll convert some bytes twice. Which is fine,
					// uppercasing already-uppercased data is a no-op.
					char* i = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(data + 16) & ~0xf);

					// Convert all aligned vectors using aligned load/store
					for(; i + 16 <= end; i += 16) {
						const __m128i chars = _mm_load_si128(reinterpret_cast<const __m128i*>(i));
						_mm_store_si128(reinterpret_cast<__m128i*>(i), uppercaseOneVector(chars));
					}

					// Handle remaining less than a vector with an unaligned load & store, again overlapping back
					// with the previous already-converted elements
					if(i < end) {
						i = end - 16;
						const __m128i chars = _mm_loadu_si128(reinterpret_cast<const __m128i*>(i));
						_mm_storeu_si128(reinterpret_cast<__m128i*>(i), uppercaseOneVector(chars));
					}
				};
			}
#endif

#if defined(DEATH_ENABLE_AVX2)
			// Trivial extension of the SSE2 code to AVX2. The only significant difference is a workaround for MinGW, see the comment below.
			DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_AVX2 typename std::decay<decltype(lowercaseInPlace)>::type lowercaseInPlaceImplementation(Cpu::Avx2T) {
				return [](char* const data, const std::size_t size) DEATH_ENABLE_AVX2 {
					char* const end = data + size;

					// If we have less than 32 bytes, fall back to the SSE variant
					if(size < 32)
						return lowercaseInPlaceImplementation(Cpu::Sse2)(data, size);

					// Core algorithm
					const __m256i aAndAbove = _mm256_set1_epi8(char(256u - std::uint8_t('A')));
					const __m256i lowest25 = _mm256_set1_epi8(25);
					const __m256i lowercaseBit = _mm256_set1_epi8(0x20);
					// Compared to the SSE2 case, this performs the operation in-place on a __m256i reference instead
					// of taking and returning it by value. This is in order to work around a MinGW / Windows GCC bug,
					// where it doesn't align __m256i instances passed to or returned from functions to 32 bytes,
					// but still uses aligned load/store for them. Reported back in 2011, still not fixed even in late 2023:
					//	https://gcc.gnu.org/bugzilla/show_bug.cgi?id=49001
					//	https://gcc.gnu.org/bugzilla/show_bug.cgi?id=54412
					const auto lowercaseOneVectorInPlace = [&](__m256i& chars) DEATH_ENABLE_AVX2 {
						// Moves 'A' and everything above to 0 and up (it overflows and wraps around)
						const __m256i uppercaseInLowest25 = _mm256_add_epi8(chars, aAndAbove);
						// Subtracts 25 with saturation, which makes the original 'A' to 'Z' (now 0 to 25) zero and everything else non-zero
						const __m256i lowest25IsZero = _mm256_subs_epu8(uppercaseInLowest25, lowest25);
						// Mask indicating where uppercase letters where, i.e. which values are now zero
						const __m256i maskUppercase = _mm256_cmpeq_epi8(lowest25IsZero, _mm256_setzero_si256());
						// For the masked chars a lowercase bit is set, and the bit is then added to the original chars,
						// making the uppercase chars lowercase
						chars = _mm256_add_epi8(chars, _mm256_and_si256(maskUppercase, lowercaseBit));
					};

					// Unconditionally convert the first vector in a slower, unaligned way. Any extra branching to avoid the unaligned
					// load & store if already aligned would be most probably more expensive than the actual operation.
					{
						__m256i chars = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data));
						lowercaseOneVectorInPlace(chars);
						_mm256_storeu_si256(reinterpret_cast<__m256i*>(data), chars);
					}

					// Go to the next aligned position. If the pointer was already aligned, we'll go to the next aligned
					// vector; if not, there will be an overlap and we'll convert some bytes twice. Which is fine,
					// lowercasing already-lowercased data is a no-op.
					char* i = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(data + 32) & ~0x1f);

					// Convert all aligned vectors using aligned load/store
					for(; i + 32 <= end; i += 32) {
						__m256i chars = _mm256_load_si256(reinterpret_cast<const __m256i*>(i));
						lowercaseOneVectorInPlace(chars);
						_mm256_store_si256(reinterpret_cast<__m256i*>(i), chars);
					}

					// Handle remaining less than a vector with an unaligned load & store, again overlapping back
					// with the previous already-converted elements
					if(i < end) {
						i = end - 32;
						__m256i chars = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(i));
						lowercaseOneVectorInPlace(chars);
						_mm256_storeu_si256(reinterpret_cast<__m256i*>(i), chars);
					}
				};
			}

			// Again just trivial extension to AVX2, and the MinGW workaround
			DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_AVX2 typename std::decay<decltype(uppercaseInPlace)>::type uppercaseInPlaceImplementation(Cpu::Avx2T) {
				return [](char* const data, const std::size_t size) DEATH_ENABLE_AVX2 {
					char* const end = data + size;

					// If we have less than 32 bytes, fall back to the SSE variant
					if(size < 32)
						return uppercaseInPlaceImplementation(Cpu::Sse2)(data, size);

					// Core algorithm
					const __m256i aAndAbove = _mm256_set1_epi8(char(256u - std::uint8_t('a')));
					const __m256i lowest25 = _mm256_set1_epi8(25);
					const __m256i lowercaseBit = _mm256_set1_epi8(0x20);
					// See the comment next to lowercaseOneVectorInPlace() above for why this is done in-place
					const auto uppercaseOneVectorInPlace = [&](__m256i& chars) DEATH_ENABLE_AVX2 {
						// Moves 'a' and everything above to 0 and up (it overflows and wraps around)
						const __m256i lowercaseInLowest25 = _mm256_add_epi8(chars, aAndAbove);
						// Subtracts 25 with saturation, which makes the original 'a' to 'z' (now 0 to 25) zero and everything else non-zero
						const __m256i lowest25IsZero = _mm256_subs_epu8(lowercaseInLowest25, lowest25);
						// Mask indicating where uppercase letters where, i.e. which values are now zero
						const __m256i maskUppercase = _mm256_cmpeq_epi8(lowest25IsZero, _mm256_setzero_si256());
						// For the masked chars a lowercase bit is set, and the bit is then subtracted from the original chars,
						// making the lowercase chars uppercase
						chars = _mm256_sub_epi8(chars, _mm256_and_si256(maskUppercase, lowercaseBit));
					};

					// Unconditionally convert the first vector in a slower, unaligned way. Any extra branching to avoid the unaligned
					// load & store if already aligned would be most probably more expensive than the actual operation.
					{
						__m256i chars = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data));
						uppercaseOneVectorInPlace(chars);
						_mm256_storeu_si256(reinterpret_cast<__m256i*>(data), chars);
					}

					// Go to the next aligned position. If the pointer was already aligned, we'll go to the next aligned
					// vector; if not, there will be an overlap and we'll convert some bytes twice. Which is fine,
					// uppercasing already-uppercased data is a no-op.
					char* i = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(data + 32) & ~0x1f);

					// Convert all aligned vectors using aligned load/store
					for(; i + 32 <= end; i += 32) {
						__m256i chars = _mm256_load_si256(reinterpret_cast<const __m256i*>(i));
						uppercaseOneVectorInPlace(chars);
						_mm256_store_si256(reinterpret_cast<__m256i*>(i), chars);
					}

					// Handle remaining less than a vector with an unaligned load & store, again overlapping back
					// with the previous already-converted elements
					if(i < end) {
						i = end - 32;
						__m256i chars = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(i));
						uppercaseOneVectorInPlace(chars);
						_mm256_storeu_si256(reinterpret_cast<__m256i*>(i), chars);
					}
				};
			}
#endif

#if defined(DEATH_ENABLE_SIMD128)
			// Trivial port of the SSE2 code to WASM SIMD. As WASM SIMD doesn't differentiate between aligned and unaligned
			// load, the load/store code is the same for both aligned and unaligned case, making everything slightly shorter.
			// The high-level operation stays the same as with SSE2 tho, even if just for memory access patterns I think
			// it still makes sense to do as much as possible aligned.
			DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_SIMD128 typename std::decay<decltype(lowercaseInPlace)>::type lowercaseInPlaceImplementation(Cpu::Simd128T) {
				return [](char* data, const std::size_t size) DEATH_ENABLE_SIMD128 {
					char* const end = data + size;

					// If we have less than 16 bytes, do it the stupid way, equivalent to the scalar variant and just unrolled.
					{
						char* j = data;
						switch(size) {
							case 15: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 14: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 13: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 12: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 11: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 10: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  9: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  8: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  7: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  6: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  5: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  4: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  3: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  2: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  1: *j += (std::uint8_t(*j - 'A') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  0: return;
						}
					}

					// Core algorithm
					const v128_t aAndAbove = wasm_i8x16_const_splat(char(256u - std::uint8_t('A')));
					const v128_t lowest25 = wasm_i8x16_const_splat(25);
					const v128_t lowercaseBit = wasm_i8x16_const_splat(0x20);
					const v128_t zero = wasm_i8x16_const_splat(0);
					const auto lowercaseOneVectorInPlace = [&](v128_t* const data) DEATH_ENABLE_SIMD128 {
						const v128_t chars = wasm_v128_load(data);
						// Moves 'A' and everything above to 0 and up (it overflows and wraps around)
						const v128_t uppercaseInLowest25 = wasm_i8x16_add(chars, aAndAbove);
						// Subtracts 25 with saturation, which makes the original 'A' to 'Z' (now 0 to 25) zero and everything else non-zero
						const v128_t lowest25IsZero = wasm_u8x16_sub_sat(uppercaseInLowest25, lowest25);
						// Mask indicating where uppercase letters where, i.e. which values are now zero
						const v128_t maskUppercase = wasm_i8x16_eq(lowest25IsZero, zero);
						// For the masked chars a lowercase bit is set, and the bit is then added to the original chars, making the uppercase chars lowercase
						wasm_v128_store(data, wasm_i8x16_add(chars, wasm_v128_and(maskUppercase, lowercaseBit)));
					};

					// Unconditionally convert the first unaligned vector
					lowercaseOneVectorInPlace(reinterpret_cast<v128_t*>(data));

					// Go to the next aligned position. If the pointer was already aligned, we'll go to the next aligned
					// vector; if not, there will be an overlap and we'll convert some bytes twice. Which is fine,
					// lowercasing already-lowercased data is a no-op.
					char* i = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(data + 16) & ~0xf);

					// Convert all aligned vectors
					for(; i + 16 <= end; i += 16)
						lowercaseOneVectorInPlace(reinterpret_cast<v128_t*>(i));

					// Handle remaining less than a vector, again overlapping back with the previous
					// already-converted elements, in an unaligned way
					if(i < end) {
						i = end - 16;
						lowercaseOneVectorInPlace(reinterpret_cast<v128_t*>(i));
					}
				};
			}

			// Again just a trivial port of the SSE2 code to WASM SIMD, with the same "aligned load/store is the same as unaligned"
			// simplification as the lowercase variant above
			DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_SIMD128 typename std::decay<decltype(uppercaseInPlace)>::type uppercaseInPlaceImplementation(Cpu::Simd128T) {
				return [](char* data, const std::size_t size) DEATH_ENABLE_SIMD128 {
					char* const end = data + size;

					// If we have less than 16 bytes, do it the stupid way, equivalent to the scalar variant and just unrolled.
					{
						char* j = data;
						switch(size) {
							case 15: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 14: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 13: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 12: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 11: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case 10: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  9: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  8: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  7: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  6: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  5: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  4: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  3: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  2: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  1: *j -= (std::uint8_t(*j - 'a') < 26) << 5; ++j; DEATH_FALLTHROUGH
							case  0: return;
						}
					}

					// Core algorithm
					const v128_t aAndAbove = wasm_i8x16_const_splat(char(256u - std::uint8_t('a')));
					const v128_t lowest25 = wasm_i8x16_const_splat(25);
					const v128_t lowercaseBit = wasm_i8x16_const_splat(0x20);
					const v128_t zero = wasm_i8x16_const_splat(0);
					const auto uppercaseOneVectorInPlace = [&](v128_t* const data) DEATH_ENABLE_SIMD128 {
						const v128_t chars = wasm_v128_load(data);
						// Moves 'a' and everything above to 0 and up (it overflows and wraps around)
						const v128_t lowercaseInLowest25 = wasm_i8x16_add(chars, aAndAbove);
						// Subtracts 25 with saturation, which makes the original 'a' to 'z' (now 0 to 25) zero and everything else non-zero
						const v128_t lowest25IsZero = wasm_u8x16_sub_sat(lowercaseInLowest25, lowest25);
						// Mask indicating where uppercase letters where, i.e. which values are now zero
						const v128_t maskUppercase = wasm_i8x16_eq(lowest25IsZero, zero);
						// For the masked chars a lowercase bit is set, and the bit is then subtracted from the original
						// chars, making the lowercase chars uppercase
						wasm_v128_store(data, wasm_i8x16_sub(chars, wasm_v128_and(maskUppercase, lowercaseBit)));
					};

					// Unconditionally convert the first unaligned vector. WASM doesn't differentiate between aligned
					// and unaligned load, it's always unaligned, however even if just for memory access patterns I think
					// it still makes sense to do as much as possible aligned, so this matches what the SSE2 code does.
					uppercaseOneVectorInPlace(reinterpret_cast<v128_t*>(data));

					// Go to the next aligned position. If the pointer was already aligned, we'll go to the next aligned vector;
					// if not, there will be an overlap and we'll convert some bytes twice. Which is fine, uppercasing
					// already-uppercased data is a no-op.
					char* i = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(data + 16) & ~0xf);

					// Convert all aligned vectors
					for(; i + 16 <= end; i += 16)
						uppercaseOneVectorInPlace(reinterpret_cast<v128_t*>(i));

					// Handle remaining less than a vector with an unaligned load & store, again overlapping back
					// with the previous already-converted elements
					if(i < end) {
						i = end - 16;
						uppercaseOneVectorInPlace(reinterpret_cast<v128_t*>(i));
					}
				};
			}
			#endif

		}

		DEATH_CPU_DISPATCHER_BASE(lowercaseInPlaceImplementation)
		DEATH_CPU_DISPATCHED(lowercaseInPlaceImplementation, void DEATH_CPU_DISPATCHED_DECLARATION(lowercaseInPlace)(char* data, std::size_t size))({
			return lowercaseInPlaceImplementation(Cpu::DefaultBase)(data, size);
		})
		DEATH_CPU_DISPATCHER_BASE(uppercaseInPlaceImplementation)
		DEATH_CPU_DISPATCHED(uppercaseInPlaceImplementation, void DEATH_CPU_DISPATCHED_DECLARATION(uppercaseInPlace)(char* data, std::size_t size))({
			return uppercaseInPlaceImplementation(Cpu::DefaultBase)(data, size);
		})
	}

	String lowercase(const StringView string) {
		// Theoretically doing the copy in the same loop as case change could be faster for *really long* strings due
		// to cache reuse, but until that proves to be a bottleneck I'll go with the simpler solution.
		// Not implementing through lowercase(Containers::String) as the call stack is deep enough already and we don't
		// need the extra checks there.
		String out{string};
		lowercaseInPlace(out);
		return out;
	}

	String lowercase(String string) {
		// In the rare scenario where we'd get a non-owned string (such as String::nullTerminatedView() passed right
		// into the function), make it owned first. Usually it'll get copied however, which already makes it owned.
		if(!string.isSmall() && string.deleter()) string = String{string};

		lowercaseInPlace(string);
		return string;
	}

	String uppercase(const StringView string) {
		// Theoretically doing the copy in the same loop as case change could be faster for *really long* strings due
		// to cache reuse, but until that proves to be a bottleneck I'll go with the simpler solution.
		// Not implementing through uppercase(Containers::String) as the call stack is deep enough already and we don't
		// need the extra checks there.
		String out{string};
		uppercaseInPlace(out);
		return out;
	}

	String uppercase(String string) {
		// In the rare scenario where we'd get a non-owned string (such as String::nullTerminatedView() passed right
		// into the function), make it owned first. Usually it'll get copied however, which already makes it owned.
		if(!string.isSmall() && string.deleter()) string = String{string};

		uppercaseInPlace(string);
		return string;
	}

	String replaceFirst(const StringView string, const StringView search, const StringView replace) {
		// Handle also the case when the search string is empty - find() returns (empty) begin in that case and we just
		// prepend the replace string.
		const StringView found = string.find(search);
		if(!search || found) {
			String output{NoInit, string.size() + replace.size() - found.size()};
			const std::size_t begin = found.begin() - string.begin();
			std::memcpy(output.data(), string.data(), begin);
			std::memcpy(output.data() + begin, replace.data(), replace.size());
			const std::size_t end = begin + search.size();
			std::memcpy(output.data() + begin + replace.size(), string.data() + end, string.size() - end);
			return output;
		}

		return string;
	}

	String replaceAll(StringView string, const StringView search, const StringView replace) {
		DEATH_ASSERT(!search.empty(), {}, "StringUtils::replaceAll(): Empty search string would cause an infinite loop");
		Array<char> output;
		while(const StringView found = string.find(search)) {
			arrayAppend(output, string.prefix(found.begin()));
			arrayAppend(output, replace);
			string = string.slice(found.end(), string.end());
		}
		arrayAppend(output, string);
		arrayAppend(output, '\0');
		const std::size_t size = output.size();
		// This assumes that the growable array uses std::malloc() (which has to be std::free()'d later) in order to be
		// able to std::realloc(). The deleter doesn't use the size argument so it should be fine to transfer it over
		// to a String with the size excluding the null terminator.
		void(*const deleter)(char*, std::size_t) = output.deleter();
		DEATH_DEBUG_ASSERT(deleter, {}, "StringUtils::replaceAll(): Invalid deleter used");
		return Containers::String{output.release(), size - 1, deleter};
	}

	String replaceAll(String string, char search, char replace) {
		// If not even a single character is found, pass the argument through unchanged
		const MutableStringView found = string.find(search);
		if(!found) return std::move(string);

		// Convert the found pointer to an index to be able to replace even after a potential reallocation below
		const std::size_t firstFoundPosition = found.begin() - string.begin();

		// Otherwise, in the rare scenario where we'd get a non-owned string (such as String::nullTerminatedView() passed
		// right into the function), make it owned first. Usually it'll get copied however, which already makes it owned.
		if(!string.isSmall() && string.deleter()) string = Containers::String{string};

		// Replace the already-found occurence and delegate the rest further
		string[firstFoundPosition] = replace;
		replaceAllInPlace(string.exceptPrefix(firstFoundPosition + 1), search, replace);
		return string;
	}

	void replaceAllInPlace(const MutableStringView string, char search, char replace) {
		for (char& c : string) {
			if (c == search) {
				c = replace;
			}
		}
	}
}
