#include "StringView.h"
#include "Array.h"
#include "ArrayView.h"
#include "GrowableArray.h"
#include "StaticArray.h"
#include "String.h"
#include "../Cpu.h"

#include <cstring>

#if (defined(DEATH_ENABLE_SSE2) || defined(DEATH_ENABLE_AVX)) && defined(DEATH_ENABLE_BMI1)
#	include "../IntrinsicsAvx.h"
#endif
#if defined(DEATH_ENABLE_NEON) && !defined(DEATH_TARGET_32BIT)
#	include <arm_neon.h>
#endif
#if defined(DEATH_ENABLE_SIMD128)
#	include <wasm_simd128.h>
#endif

namespace Death { namespace Containers {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	template<class T> BasicStringView<T>::BasicStringView(T* const data, const StringViewFlags flags, std::nullptr_t) noexcept : BasicStringView{data,
		data ? std::strlen(data) : 0,
		flags | (data ? StringViewFlags::NullTerminated : StringViewFlags::Global)} {}

	template<class T> BasicStringView<T>::BasicStringView(String& string) noexcept : BasicStringView{string.data(), string.size(), string.viewFlags()} {}

	template<> template<> BasicStringView<const char>::BasicStringView(const String& string) noexcept : BasicStringView{string.data(), string.size(), string.viewFlags()} {}

	template<class T> Array<BasicStringView<T>> BasicStringView<T>::split(const char delimiter) const {
		Array<BasicStringView<T>> parts;
		T* const end = this->end();
		T* oldpos = _data;
		T* pos;
		while (oldpos < end && (pos = static_cast<T*>(std::memchr(oldpos, delimiter, end - oldpos)))) {
			arrayAppend(parts, slice(oldpos, pos));
			oldpos = pos + 1;
		}

		if (!empty())
			arrayAppend(parts, suffix(oldpos));

		return parts;
	}

	template<class T> Array<BasicStringView<T>> BasicStringView<T>::splitWithoutEmptyParts(const char delimiter) const {
		Array<BasicStringView<T>> parts;
		T* const end = this->end();
		T* oldpos = _data;
		while (oldpos < end) {
			T* pos = static_cast<T*>(std::memchr(oldpos, delimiter, end - oldpos));
			// Not sure why memchr can't just do this, it would make much more sense
			if (!pos) pos = end;

			if (pos != oldpos)
				arrayAppend(parts, slice(oldpos, pos));

			oldpos = pos + 1;
		}

		return parts;
	}

	namespace Implementation
	{
		const char* stringFindString(const char* data, const std::size_t size, const char* const substring, const std::size_t substringSize) {
			// If the substring is not larger than the string we search in
			if (substringSize <= size) {
				if (!size) return data;

				// Otherwise compare it with the string at all possible positions in the string until we have a match.
				for (const char* const max = data + size - substringSize; data <= max; ++data) {
					if (std::memcmp(data, substring, substringSize) == 0)
						return data;
				}
			}

			// If the substring is larger or no match was found, fail
			return { };
		}
	}

	template<class T> Array<BasicStringView<T>> BasicStringView<T>::split(const StringView delimiter) const {
		const char* const delimiterData = delimiter.data();
		const std::size_t delimiterSize = delimiter.size();
		DEATH_ASSERT(delimiterSize, {}, "Containers::StringView::split(): delimiter is empty");

		Array<BasicStringView<T>> parts;
		const char* const end = this->end();
		const char* oldpos = _data;
		const char* pos;
		while (oldpos < end && (pos = Implementation::stringFindString(oldpos, end - oldpos, delimiterData, delimiterSize))) {
			arrayAppend(parts, slice(const_cast<T*>(oldpos), const_cast<T*>(pos)));
			oldpos = pos + delimiterSize;
		}

		if (!empty())
			arrayAppend(parts, suffix(const_cast<T*>(oldpos)));

		return parts;
	}

	namespace Implementation
	{
		const char* stringFindLastString(const char* const data, const std::size_t size, const char* const substring, const std::size_t substringSize) {
			// If the substring is not larger than the string we search in
			if (substringSize <= size) {
				if (!size) return data;

				// Otherwise compare it with the string at all possible positions in the string until we have a match.
				for (const char* i = data + size - substringSize; i >= data; --i) {
					if (std::memcmp(i, substring, substringSize) == 0)
						return i;
				}
			}

			// If the substring is larger or no match was found, fail
			return { };
		}

		namespace
		{
			/* SIMD implementation of character lookup. Loosely based off
			   https://docs.rs/memchr/2.3.4/src/memchr/x86/sse2.rs.html, which in turn is
			   based off https://gms.tf/stdfind-and-memchr-optimizations.html, which at the
			   time of writing (Jul 2022) uses m.css, so the circle is complete :))
			   The code below is commented, but the core points are the following:
				1.  do as much as possible via aligned loads,
				2.  otherwise, do as much as possible via unaligned vector loads even at
					the cost of ovelapping with an aligned load,
				3.  otherwise, fall back to a smaller vector width (AVX -> SSE) or to a
					scalar code
			   The 128-bit variant first checks if there's less than 16 bytes. If it is, it
			   just checks each of them sequentially. Otherwise, with 16 and more bytes,
			   the following is done:
				  +---+                         +---+
				  | A |                         | D |
				  +---+                         +---+
					+---+---+---+---+     +---+--
					| B :   :   :   | ... | C | ...
					+---+---+---+---+     +---+--
				A.  First it does an unconditional unaligned load of a single vector
					(assuming an extra conditional branch would likely be slower than the
					unaligned load ovehead), compares all bytes inside to the (broadcasted)
					search value and for all bytes that are equal calculates a bitmask (if
					4th and 7th byte is present, the bitmask has bit 4 and 7 set). Then, if
					any bit is set, returns the  position of the first bit which is the
					found index.
				B.  Next it finds an aligned position. If the vector A was already aligned,
					it will start right after, otherwise there may be up to 15 bytes
					overlap that'll be checked twice. From the aligned position, to avoid
					branching too often, it goes in a batch of four vectors at a time,
					checking the result together for all four. Which also helps offset the
					extra work from the initial overlap.
				C.  Once there is less than four vectors left, it goes vector-by-vector,
					still doing aligned loads, but branching for every.
				D.  Once there's less than 16 bytes left, it performs an unaligned load
					that may overlap with the previous aligned vector, similarly to the
					initial unaligned load A.
				The 256-bit variant is mostly just about expanding from 16 bytes at a time
				to 32 bytes at a time. The only difference is that instead of doing a
				scalar fallback for less than 32 bytes, it delegates to the 128-bit
				variant --- effectively performing the lookup with either two overlapping
				16-byte vectors (or falling back to scalar for less than 16 bytes).
				The ARM variant has the high-level concept similar to x86, except that NEON
				doesn't have a bitmask instruction. Instead a "right shift and narrow"
				instruction is used, see comments there for details.
				The WASM variant is mostly a direct translation of the x86 variant, except
				as noted in code comments. */

#if defined(DEATH_ENABLE_SSE2) && defined(DEATH_ENABLE_BMI1)
			DEATH_ENABLE(SSE2, BMI1) DEATH_ALWAYS_INLINE const char* findCharacterSingleVectorUnaligned(Cpu::Sse2T, const char* at, const __m128i vn1) {
				// _mm_lddqu_si128 is just an alias to _mm_loadu_si128 on all CPUs with SSSE3+, no reason to use it: https://stackoverflow.com/a/38383624
				const __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(at));
				if (const int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk, vn1)))
					return at + _tzcnt_u32(mask);
				return { };
			}
			DEATH_ENABLE(SSE2, BMI1) DEATH_ALWAYS_INLINE const char* findCharacterSingleVector(Cpu::Sse2T, const char* at, const __m128i vn1) {
				const __m128i chunk = _mm_load_si128(reinterpret_cast<const __m128i*>(at));
				if (const int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk, vn1)))
					return at + _tzcnt_u32(mask);
				return { };
			}

			DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE(SSE2, BMI1) typename std::decay<decltype(stringFindCharacter)>::type stringFindCharacterImplementation(DEATH_CPU_DECLARE(Cpu::Sse2 | Cpu::Bmi1)) {
				// Can't use trailing return type due to a GCC 9.3 bug, which is the default on Ubuntu 20.04: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=90333
				return [](const char* const data, const std::size_t size, const char character) DEATH_ENABLE(SSE2, BMI1) {
					const char* const end = data + size;

					/* If we have less than 16 bytes, do it the stupid way. Compared to a plain
					   loop this is 1.5-2x faster when unrolled. Interestingly enough, on GCC
					   (11) doing a pre-increment and `return j` leads to
						lea    0x1(%rcx),%rax
						mov    %rax,%r8
						cmp    0x1(%rcx),%dl
						je     0x63f43 <+243>
					   repeated 15 times (with <+243> returning %r8 for all), while a
					   post-increment and `return j - 1` is just
						lea    0x1(%rax),%rcx
						cmp    (%rax),%dl
						je     0x63f20 <+208>
					   with %rax and %rcx alternating in every case and the jump always
					   different. That's 25% instructions less for the post-increment, and the
					   benchmark confirms that (~3.50 vs ~2.80 µs). Clang (13) does a similar
					   thing, although it has `lea, cmp, mov, je` in the first case instead and
					   `cmp, je, add` in the second case instead, and (probably due to the
					   different order?) the benchmark doesn't show any difference between the
					   two. Since post-increment significantly helps GCC and doesn't make
					   Clang slower, use it. */
					{
						const char* j = data;
						switch (size) {
							case 15: if (*j++ == character) return j - 1; DEATH_FALLTHROUGH
							case 14: if (*j++ == character) return j - 1; DEATH_FALLTHROUGH
							case 13: if (*j++ == character) return j - 1; DEATH_FALLTHROUGH
							case 12: if (*j++ == character) return j - 1; DEATH_FALLTHROUGH
							case 11: if (*j++ == character) return j - 1; DEATH_FALLTHROUGH
							case 10: if (*j++ == character) return j - 1; DEATH_FALLTHROUGH
							case  9: if (*j++ == character) return j - 1; DEATH_FALLTHROUGH
							case  8: if (*j++ == character) return j - 1; DEATH_FALLTHROUGH
							case  7: if (*j++ == character) return j - 1; DEATH_FALLTHROUGH
							case  6: if (*j++ == character) return j - 1; DEATH_FALLTHROUGH
							case  5: if (*j++ == character) return j - 1; DEATH_FALLTHROUGH
							case  4: if (*j++ == character) return j - 1; DEATH_FALLTHROUGH
							case  3: if (*j++ == character) return j - 1; DEATH_FALLTHROUGH
							case  2: if (*j++ == character) return j - 1; DEATH_FALLTHROUGH
							case  1: if (*j++ == character) return j - 1; DEATH_FALLTHROUGH
							case  0: return static_cast<const char*>(nullptr);
						}
					}

					const __m128i vn1 = _mm_set1_epi8(character);

					// Unconditionally do a lookup in the first vector a slower, unaligned way. Any extra branching to avoid
					// the unaligned load if already aligned would be most probably more expensive than the actual unaligned load.
					if (const char* const found = findCharacterSingleVectorUnaligned(Cpu::Sse2, data, vn1))
						return found;

					// Go to the next aligned position. If the pointer was already aligned, we'll go to the next aligned vector;
					// if not, there will be an overlap and we'll check some bytes twice.
					const char* i = reinterpret_cast<const char*>(reinterpret_cast<std::uintptr_t>(data + 16) & ~0xf);

					// Go four vectors at a time with the aligned pointer
					for (; i + 4 * 16 < end; i += 4 * 16) {
						const __m128i a = _mm_load_si128(reinterpret_cast<const __m128i*>(i) + 0);
						const __m128i b = _mm_load_si128(reinterpret_cast<const __m128i*>(i) + 1);
						const __m128i c = _mm_load_si128(reinterpret_cast<const __m128i*>(i) + 2);
						const __m128i d = _mm_load_si128(reinterpret_cast<const __m128i*>(i) + 3);

						const __m128i eqa = _mm_cmpeq_epi8(vn1, a);
						const __m128i eqb = _mm_cmpeq_epi8(vn1, b);
						const __m128i eqc = _mm_cmpeq_epi8(vn1, c);
						const __m128i eqd = _mm_cmpeq_epi8(vn1, d);

						const __m128i or1 = _mm_or_si128(eqa, eqb);
						const __m128i or2 = _mm_or_si128(eqc, eqd);
						const __m128i or3 = _mm_or_si128(or1, or2);
						if (_mm_movemask_epi8(or3)) {
							if (const int mask = _mm_movemask_epi8(eqa))
								return i + 0 * 16 + _tzcnt_u32(mask);
							if (const int mask = _mm_movemask_epi8(eqb))
								return i + 1 * 16 + _tzcnt_u32(mask);
							if (const int mask = _mm_movemask_epi8(eqc))
								return i + 2 * 16 + _tzcnt_u32(mask);
							if (const int mask = _mm_movemask_epi8(eqd))
								return i + 3 * 16 + _tzcnt_u32(mask);
							// Unreachable
						}
					}

					// Handle remaining less than four vectors
					for (; i + 16 <= end; i += 16)
						if (const char* const found = findCharacterSingleVector(Cpu::Sse2, i, vn1))
							return found;

					// Handle remaining less than a vector with an unaligned search, again overlapping back
					// with the previous already-searched elements
					if (i < end) {
						i = end - 16;
						return findCharacterSingleVectorUnaligned(Cpu::Sse2, i, vn1);
					}

					return static_cast<const char*>(nullptr);
				};
			}
#endif

#if defined(DEATH_ENABLE_AVX2) && defined(DEATH_ENABLE_BMI1)
			DEATH_ENABLE(AVX2, BMI1) DEATH_ALWAYS_INLINE const char* findCharacterSingleVectorUnaligned(Cpu::Avx2T, const char* at, const __m256i vn1) {
				// _mm256_lddqu_si256 is just an alias to _mm256_loadu_si256, no reason to use it: https://stackoverflow.com/a/47426790
				const __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(at));
				if (const int mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, vn1)))
					return at + _tzcnt_u32(mask);
				return { };
			}
			DEATH_ENABLE(AVX2, BMI1) DEATH_ALWAYS_INLINE const char* findCharacterSingleVector(Cpu::Avx2T, const char* at, const __m256i vn1) {
				const __m256i chunk = _mm256_load_si256(reinterpret_cast<const __m256i*>(at));
				if (const int mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, vn1)))
					return at + _tzcnt_u32(mask);
				return { };
			}

			DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE(AVX2, BMI1) typename std::decay<decltype(stringFindCharacter)>::type stringFindCharacterImplementation(DEATH_CPU_DECLARE(Cpu::Avx2 | Cpu::Bmi1)) {
				// Can't use trailing return type due to a GCC 9.3 bug, which is the default on Ubuntu 20.04: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=90333
				return [](const char* const data, const std::size_t size, const char character) DEATH_ENABLE(AVX2, BMI1) {
					const char* const end = data + size;

					// If we have less than 32 bytes, fall back to the SSE variant
					if (size < 32)
						return stringFindCharacterImplementation(DEATH_CPU_SELECT(Cpu::Sse2 | Cpu::Bmi1))(data, size, character);

					const __m256i vn1 = _mm256_set1_epi8(character);

					// Unconditionally do a lookup in the first vector a slower, unaligned way. Any extra branching to avoid
					// the unaligned load if already aligned would be most probably more expensive than the actual unaligned load.
					if (const char* const found = findCharacterSingleVectorUnaligned(Cpu::Avx2, data, vn1))
						return found;

					// Go to the next aligned position. If the pointer was already aligned, we'll go to the next aligned vector;
					// if not, there will be an overlap and we'll check some bytes twice.
					const char* i = reinterpret_cast<const char*>(reinterpret_cast<std::uintptr_t>(data + 32) & ~0x1f);

					// Go four vectors at a time with the aligned pointer
					for (; i + 4 * 32 < end; i += 4 * 32) {
						const __m256i a = _mm256_load_si256(reinterpret_cast<const __m256i*>(i) + 0);
						const __m256i b = _mm256_load_si256(reinterpret_cast<const __m256i*>(i) + 1);
						const __m256i c = _mm256_load_si256(reinterpret_cast<const __m256i*>(i) + 2);
						const __m256i d = _mm256_load_si256(reinterpret_cast<const __m256i*>(i) + 3);

						const __m256i eqa = _mm256_cmpeq_epi8(vn1, a);
						const __m256i eqb = _mm256_cmpeq_epi8(vn1, b);
						const __m256i eqc = _mm256_cmpeq_epi8(vn1, c);
						const __m256i eqd = _mm256_cmpeq_epi8(vn1, d);

						const __m256i or1 = _mm256_or_si256(eqa, eqb);
						const __m256i or2 = _mm256_or_si256(eqc, eqd);
						const __m256i or3 = _mm256_or_si256(or1, or2);
						if (_mm256_movemask_epi8(or3)) {
							if (const int mask = _mm256_movemask_epi8(eqa))
								return i + 0 * 32 + _tzcnt_u32(mask);
							if (const int mask = _mm256_movemask_epi8(eqb))
								return i + 1 * 32 + _tzcnt_u32(mask);
							if (const int mask = _mm256_movemask_epi8(eqc))
								return i + 2 * 32 + _tzcnt_u32(mask);
							if (const int mask = _mm256_movemask_epi8(eqd))
								return i + 3 * 32 + _tzcnt_u32(mask);
							// Unreachable
						}
					}

					// Handle remaining less than four vectors
					for (; i + 32 <= end; i += 32)
						if (const char* const found = findCharacterSingleVector(Cpu::Avx2, i, vn1))
							return found;

					// Handle remaining less than a vector with an unaligned search, again overlapping back with the previous
					// already-searched elements
					if (i < end) {
						i = end - 32;
						return findCharacterSingleVectorUnaligned(Cpu::Avx2, i, vn1);
					}

					return static_cast<const char*>(nullptr);
				};
			}
#endif

#if defined(DEATH_ENABLE_NEON) && !defined(DEATH_TARGET_32BIT)
#	if defined(DEATH_TARGET_MSVC)
			// Redirect GCC/Clang intrinsics to MSVC built-in function
			static DEATH_ALWAYS_INLINE int __builtin_ctzll(uint64_t x) {
				unsigned long index;
				_BitScanForward64(&index, x);
				return (int)index;
			}
#	endif

			// `vshrn_n_u16` and `vaddvq_u8` are missing in `armeabi-v7a` on Android, so enable it only on ARM64.
			// ARM64 doesn't differentiate between aligned and unaligned loads. ARM32 does, but it's not exposed in the intrinsics,
			// only in compiler-specific ways. Since 32-bit ARM is increasingly rare (and this code doesn't work on
			// it anyway), not bothering at all. https://stackoverflow.com/a/53245244
			DEATH_ENABLE(NEON) DEATH_ALWAYS_INLINE const char* findCharacterSingleVectorUnaligned(Cpu::NeonT, const char* at, const uint8x16_t vn1) {
				const uint8x16_t chunk = vld1q_u8(reinterpret_cast<const std::uint8_t*>(at));

				/* Emulating _mm_movemask_epi8() on ARM is rather expensive, even the most
				   optimized variant listed at https://github.com/WebAssembly/simd/pull/201
				   is 6+ instructions. Instead, a "shift right and narrow" is used, based
				   on an idea from https://twitter.com/Danlark1/status/1539344281336422400
				   and further explained in https://github.com/facebook/zstd/pull/3139.
				   First, similarly to x86, an equivalence mask is calculated with bytes
				   being either ff or 00 based on whether they match:
					00 ff ff 00 00 00 ff ff 00 00 00 00 ff 00 00 00
				   The result is reinterpreted as 8 16bit values:
					00ff  ff00  0000  ffff  0000  0000  ff00  0000
				   Then, the vshrn_n_u16() instruction shifts each 16bit value four bits to
				   the right, and drops the high half:
					000f  0ff0  0000  0fff  0000  0000  0ff0  0000
					  0f    f0    00    ff    00    00    f0    00
				   The result, stored in the lower half of a 128-bit register, is then
				   extracted as a single 64-bit number:
					0ff0 00ff 0000 f000
				   This effectively reduces the original 128-bit mask to a half, with every
				   four bits describing a masked byte. While that's still 4x more than what
				   _mm_movemask_epi8() produces, it can be tested against zero using
				   regular scalar operations. Finally, `__builtin_ctzll(mask) >> 2` is
				   equivalent to what TZCNT on a 16bit mask produced by _mm_movemask_epi8()
				   would return -- there's simply just 4x more bits. */
				const uint16x8_t eq16 = vreinterpretq_u16_u8(vceqq_u8(chunk, vn1));
				const uint64x1_t shrn64 = vreinterpret_u64_u8(vshrn_n_u16(eq16, 4));
				if (const uint64_t mask = vget_lane_u64(shrn64, 0))
					return at + (__builtin_ctzll(mask) >> 2);
				return {};
			}

			DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE(NEON) typename std::decay<decltype(stringFindCharacter)>::type stringFindCharacterImplementation(DEATH_CPU_DECLARE(Cpu::Neon)) {
				// Can't use trailing return type due to a GCC 9.3 bug, which is the default on Ubuntu 20.04: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=90333
				return[](const char* const data, const std::size_t size, const char character) DEATH_ENABLE(NEON) {
					const char* const end = data + size;

					// If we have less than 16 bytes, do it the stupid way. Unlike x86 or WASM, unrolling
					// the loop here makes things actually worse.
					if (size < 16) {
						for (const char* i = data; i != end; ++i)
							if (*i == character) return i;
						return static_cast<const char*>(nullptr);
					}

					const uint8x16_t vn1 = vdupq_n_u8(character);

					// Unconditionally do a lookup in the first vector a slower, unaligned way. Any extra branching to avoid
					// the unaligned load if already aligned would be most probably more expensive than the actual unaligned load.
					if (const char* const found = findCharacterSingleVectorUnaligned(Cpu::Neon, data, vn1))
						return found;

					// Go to the next aligned position. If the pointer was already aligned, we'll go to the next aligned vector;
					// if not, there will be an overlap and we'll check some bytes twice.
					const char* i = reinterpret_cast<const char*>(reinterpret_cast<std::uintptr_t>(data + 16) & ~0xf);

					// Go four vectors at a time with the aligned pointer
					for (; i + 4 * 16 < end; i += 4 * 16) {
						const uint8x16_t a = vld1q_u8(reinterpret_cast<const std::uint8_t*>(i) + 0 * 16);
						const uint8x16_t b = vld1q_u8(reinterpret_cast<const std::uint8_t*>(i) + 1 * 16);
						const uint8x16_t c = vld1q_u8(reinterpret_cast<const std::uint8_t*>(i) + 2 * 16);
						const uint8x16_t d = vld1q_u8(reinterpret_cast<const std::uint8_t*>(i) + 3 * 16);

						const uint8x16_t eqa = vceqq_u8(vn1, a);
						const uint8x16_t eqb = vceqq_u8(vn1, b);
						const uint8x16_t eqc = vceqq_u8(vn1, c);
						const uint8x16_t eqd = vceqq_u8(vn1, d);

						// Similar to findCharacterSingleVectorUnaligned(Cpu::NeonT), except that four "shift right
						// and narrow" operations are done, interleaving the result into two registers instead of four
						const uint8x8_t maska = vshrn_n_u16(vreinterpretq_u16_u8(eqa), 4);
						const uint8x16_t maskab = vshrn_high_n_u16(maska, vreinterpretq_u16_u8(eqb), 4);
						const uint8x8_t maskc = vshrn_n_u16(vreinterpretq_u16_u8(eqc), 4);
						const uint8x16_t maskcd = vshrn_high_n_u16(maskc, vreinterpretq_u16_u8(eqd), 4);

						// Which makes it possible to test with just one OR and a horizontal add instead of three ORs
						// and a horizontal add
						if (vaddvq_u8(vorrq_u8(maskab, maskcd))) {
							if (const std::uint64_t mask = vgetq_lane_u64(vreinterpretq_u64_u8(maskab), 0))
								return i + 0 * 16 + (__builtin_ctzll(mask) >> 2);
							if (const std::uint64_t mask = vgetq_lane_u64(vreinterpretq_u64_u8(maskab), 1))
								return i + 1 * 16 + (__builtin_ctzll(mask) >> 2);
							if (const std::uint64_t mask = vgetq_lane_u64(vreinterpretq_u64_u8(maskcd), 0))
								return i + 2 * 16 + (__builtin_ctzll(mask) >> 2);
							if (const std::uint64_t mask = vgetq_lane_u64(vreinterpretq_u64_u8(maskcd), 1))
								return i + 3 * 16 + (__builtin_ctzll(mask) >> 2);
							// Unreachable
						}
					}

					// Handle remaining less than four vectors
					for (; i + 16 <= end; i += 16)
						if (const char* const found = findCharacterSingleVectorUnaligned(Cpu::Neon, i, vn1))
							return found;

					// Handle remaining less than a vector with an unaligned search, again overlapping back with
					// the previous already-searched elements
					if (i < end) {
						i = end - 16;
						return findCharacterSingleVectorUnaligned(Cpu::Neon, i, vn1);
					}

					return static_cast<const char*>(nullptr);
				};
			}
#endif

#if defined(DEATH_ENABLE_SIMD128)
			// WASM doesn't differentiate between aligned and unaligned load, it's always unaligned :(
			DEATH_ENABLE_SIMD128 DEATH_ALWAYS_INLINE const char* findCharacterSingleVectorUnaligned(Cpu::Simd128T, const char* at, const v128_t vn1) {
				const v128_t chunk = wasm_v128_load(at);
				if (const int mask = wasm_i8x16_bitmask(wasm_i8x16_eq(chunk, vn1)))
					return at + __builtin_ctz(mask);
				return {};
			}

			DEATH_CPU_MAYBE_UNUSED typename std::decay<decltype(stringFindCharacter)>::type stringFindCharacterImplementation(DEATH_CPU_DECLARE(Cpu::Simd128)) {
				return[](const char* const data, const std::size_t size, const char character) DEATH_ENABLE_SIMD128 -> const char* {
					const char* const end = data + size;

					// If we have less than 16 bytes, do it the stupid way. Compared to a plain loop, this is 25% faster when unrolled.
					// Strangely enough, if the switch is put into an external always inline function to avoid duplication with
					// the SSE2 variant, it no longer gives the advantage. Furthermore, the post-increment optimization from the x86 case
					// doesn't help here at all, on the contrary makes the code slightly slower.
					{
						const char* j = data - 1;
						switch (size) {
							case 15: if (*++j == character) return j; DEATH_FALLTHROUGH
							case 14: if (*++j == character) return j; DEATH_FALLTHROUGH
							case 13: if (*++j == character) return j; DEATH_FALLTHROUGH
							case 12: if (*++j == character) return j; DEATH_FALLTHROUGH
							case 11: if (*++j == character) return j; DEATH_FALLTHROUGH
							case 10: if (*++j == character) return j; DEATH_FALLTHROUGH
							case  9: if (*++j == character) return j; DEATH_FALLTHROUGH
							case  8: if (*++j == character) return j; DEATH_FALLTHROUGH
							case  7: if (*++j == character) return j; DEATH_FALLTHROUGH
							case  6: if (*++j == character) return j; DEATH_FALLTHROUGH
							case  5: if (*++j == character) return j; DEATH_FALLTHROUGH
							case  4: if (*++j == character) return j; DEATH_FALLTHROUGH
							case  3: if (*++j == character) return j; DEATH_FALLTHROUGH
							case  2: if (*++j == character) return j; DEATH_FALLTHROUGH
							case  1: if (*++j == character) return j; DEATH_FALLTHROUGH
							case  0: return {};
						}
					}

					const v128_t vn1 = wasm_i8x16_splat(character);

					// Unconditionally do a lookup in the first vector a slower, unaligned way. Any extra branching to avoid
					// the unaligned load if already aligned would be most probably more expensive than the actual unaligned load.
					if (const char* const found = findCharacterSingleVectorUnaligned(Cpu::Simd128, data, vn1))
						return found;

					// Go to the next aligned position. If the pointer was already aligned, we'll go to the next aligned vector;
					// if not, there will be an overlap and we'll check some bytes twice.
					const char* i = reinterpret_cast<const char*>(reinterpret_cast<std::uintptr_t>(data + 16) & ~0xf);

					// Go four vectors at a time with the aligned pointer
					for (; i + 4 * 16 < end; i += 4 * 16) {
						const v128_t a = wasm_v128_load(reinterpret_cast<const v128_t*>(i) + 0);
						const v128_t b = wasm_v128_load(reinterpret_cast<const v128_t*>(i) + 1);
						const v128_t c = wasm_v128_load(reinterpret_cast<const v128_t*>(i) + 2);
						const v128_t d = wasm_v128_load(reinterpret_cast<const v128_t*>(i) + 3);

						const v128_t eqa = wasm_i8x16_eq(vn1, a);
						const v128_t eqb = wasm_i8x16_eq(vn1, b);
						const v128_t eqc = wasm_i8x16_eq(vn1, c);
						const v128_t eqd = wasm_i8x16_eq(vn1, d);

						const v128_t or1 = wasm_v128_or(eqa, eqb);
						const v128_t or2 = wasm_v128_or(eqc, eqd);
						const v128_t or3 = wasm_v128_or(or1, or2);
						// wasm_i8x16_bitmask(or3) maps directly to the SSE2 variant and is thus fast on x86, but on ARM wasm_v128_any_true(or3)
						// is faster. With StringViewBenchmark::findCharacterRare() and runtime dispatch disabled for tests, on x86 (node.js 17.8)
						// bitmask is ~1.35 µs and any_true ~1.85 µs; on ARM (Huawei P10, Vivaldi w/ Chromium 102) bitmask is 14.3 µs
						// and any_true 11.7 µs. Ideally we'd have two runtime versions, one picking x86-friendly instructions
						// and the other ARM-friendly, but function pointer dispatch has a *massive* overhead currently. Related info
						// about instruction complexity:
						// https://github.com/WebAssembly/simd/pull/201
						// https://github.com/zeux/wasm-simd/blob/master/Instructions.md */
						if (wasm_i8x16_bitmask(or3)) {
							if (const int mask = wasm_i8x16_bitmask(eqa))
								return i + 0 * 16 + __builtin_ctz(mask);
							if (const int mask = wasm_i8x16_bitmask(eqb))
								return i + 1 * 16 + __builtin_ctz(mask);
							if (const int mask = wasm_i8x16_bitmask(eqc))
								return i + 2 * 16 + __builtin_ctz(mask);
							if (const int mask = wasm_i8x16_bitmask(eqd))
								return i + 3 * 16 + __builtin_ctz(mask);
							// Unreachable
						}
					}

					// Handle remaining less than four vectors
					for (; i + 16 <= end; i += 16)
						if (const char* const found = findCharacterSingleVectorUnaligned(Cpu::Simd128, i, vn1))
							return found;

					// Handle remaining less than a vector with an unaligned search, again overlapping back with the previous
					// already-searched elements
					if (i < end) {
						i = end - 16;
						return findCharacterSingleVectorUnaligned(Cpu::Simd128, i, vn1);
					}

					return { };
				};
			}
#endif

			DEATH_CPU_MAYBE_UNUSED typename std::decay<decltype(stringFindCharacter)>::type stringFindCharacterImplementation(DEATH_CPU_DECLARE(Cpu::Scalar)) {
				return [](const char* const data, const std::size_t size, const char character) -> const char* {
					// Yet again I'm not sure if null pointers are allowed and cppreference says nothing about that, so this might need to get patched
					return static_cast<const char*>(std::memchr(data, character, size));
				};
			}
		}

#if defined(DEATH_TARGET_X86)
		DEATH_CPU_DISPATCHER(stringFindCharacterImplementation, Cpu::Bmi1)
#else
		DEATH_CPU_DISPATCHER(stringFindCharacterImplementation)
#endif
		DEATH_CPU_DISPATCHED(stringFindCharacterImplementation, const char* DEATH_CPU_DISPATCHED_DECLARATION(stringFindCharacter)(const char* data, std::size_t size, char character))({
			return stringFindCharacterImplementation(DEATH_CPU_SELECT(Cpu::Default))(data, size, character);
		})

		const char* stringFindLastCharacter(const char* const data, const std::size_t size, const char character) {
			// Linux has a memrchr() function but other OSes not. So let's just do it myself, that way I also don't need
			// to worry about null pointers being allowed or not ... haha, well, except that if data is nullptr,
			// `*(data - 1)` blows up, so I actually need to.
			if (data) for (const char* i = data + size - 1; i >= data; --i)
				if (*i == character) return i;
			return { };
		}

		/* I don't want to include <algorithm> just for std::find_first_of() and
			unfortunately there's no equivalent in the C string library. Coming close
			are strpbrk() or strcspn() but both of them work with null-terminated
			strings, which is absolutely useless here, not to mention that both do
			*exactly* the same thing, with one returning a pointer but the other an
			offset, so what's the point of having both? What the hell. And there's no
			memcspn() or whatever which would take explicit lengths. Which means I'm
			left to my own devices. Looking at how strpbrk() / strcspn() is done, it
			ranges from trivial code:

			https://github.com/bminor/newlib/blob/6497fdfaf41d47e835fdefc78ecb0a934875d7cf/newlib/libc/string/strcspn.c
			
			to extremely optimized machine-specific code (don't look, it's GPL):
			
			https://github.com/bminor/glibc/blob/43b1048ab9418e902aac8c834a7a9a88c501620a/sysdeps/x86_64/multiarch/strcspn-c.c
			
			and the only trick I realized above the nested loop is using memchr() in an
			inverse way. In all honesty, I think that'll still be *at least* as fast as
			std::find_first_of() because I doubt STL implementations explicitly optimize
			for that case. Yes, std::string::find_first_of() probably would have that,
			but I'd first need to allocate to make use of that and FUCK NO. */
		const char* stringFindAny(const char* const data, const std::size_t size, const char* const characters, const std::size_t characterCount) {
			for (const char* i = data, *end = data + size; i != end; ++i)
				if (std::memchr(characters, *i, characterCount)) return i;
			return { };
		}

		// Variants of the above. Not sure if those even have any vaguely corresponding C lib API. Probably not.

		const char* stringFindLastAny(const char* const data, const std::size_t size, const char* const characters, const std::size_t characterCount) {
			for (const char* i = data + size; i != data; --i)
				if (std::memchr(characters, *(i - 1), characterCount)) return i - 1;
			return { };
		}

		const char* stringFindNotAny(const char* const data, const std::size_t size, const char* const characters, const std::size_t characterCount) {
			for (const char* i = data, *end = data + size; i != end; ++i)
				if (!std::memchr(characters, *i, characterCount)) return i;
			return { };
		}

		const char* stringFindLastNotAny(const char* const data, const std::size_t size, const char* const characters, const std::size_t characterCount) {
			for (const char* i = data + size; i != data; --i)
				if (!std::memchr(characters, *(i - 1), characterCount)) return i - 1;
			return { };
		}
	}

	template<class T> Array<BasicStringView<T>> BasicStringView<T>::splitOnAnyWithoutEmptyParts(const StringView delimiters) const {
		Array<BasicStringView<T>> parts;
		const char* const characters = delimiters._data;
		const std::size_t characterCount = delimiters.size();
		T* oldpos = _data;
		T* const end = _data + size();

		while (oldpos < end) {
			if (T* const pos = const_cast<T*>(Implementation::stringFindAny(oldpos, end - oldpos, characters, characterCount))) {
				if (pos != oldpos)
					arrayAppend(parts, slice(oldpos, pos));
				oldpos = pos + 1;
			} else {
				arrayAppend(parts, slice(oldpos, end));
				break;
			}
		}

		return parts;
	}

	namespace
	{
		/* If I use an externally defined view in splitWithoutEmptyParts(),
		   trimmed() and elsewhere, MSVC (2015, 2017, 2019) will blow up on the
		   explicit template instantiation with

			error C2946: explicit instantiation; 'Death::Containers::BasicStringView<const char>::<lambda_e55a1a450af96fadfe37cfb50a99d6f7>' is not a template-class specialization

		   I spent an embarrassing amount of time trying to find what lambda it
		   doesn't like, reimplemented std::find_first_of() used in
		   splitWithoutEmptyParts(), added a non-asserting variants of slice() etc,
		   but nothing helped. */
#if !defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_CLANG_CL) || _MSC_VER >= 1930 /* MSVC 2022 works */
		using namespace Literals;
		constexpr StringView Whitespace = " \t\f\v\r\n"_s;
#else
#	define WHITESPACE_MACRO_BECAUSE_MSVC_IS_STUPID " \t\f\v\r\n"_s
#endif
	}

	template<class T> Array<BasicStringView<T>> BasicStringView<T>::splitOnWhitespaceWithoutEmptyParts() const {
#if !defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_CLANG_CL) || _MSC_VER >= 1930 /* MSVC 2022 works */
		return splitOnAnyWithoutEmptyParts(Whitespace);
#else
		using namespace Literals;
		return splitOnAnyWithoutEmptyParts(WHITESPACE_MACRO_BECAUSE_MSVC_IS_STUPID);
#endif
	}

	template<class T> StaticArray<3, BasicStringView<T>> BasicStringView<T>::partition(const char separator) const {
		const std::size_t size = this->size();
		T* const pos = static_cast<T*>(std::memchr(_data, separator, size));
		return {
			pos ? prefix(pos) : *this,
			pos ? slice(pos, pos + 1) : exceptPrefix(size),
			pos ? suffix(pos + 1) : exceptPrefix(size)
		};
	}

	template<class T> StaticArray<3, BasicStringView<T>> BasicStringView<T>::partition(const StringView separator) const {
		const char* const separatorData = separator.data();
		const std::size_t separatorSize = separator.size();
		const std::size_t size = this->size();
		T* const pos = const_cast<T*>(Implementation::stringFindString(_data, size, separatorData, separatorSize));
		return {
			pos ? prefix(pos) : *this,
			pos ? slice(pos, pos + separatorSize) : exceptPrefix(size),
			pos ? suffix(pos + separatorSize) : exceptPrefix(size)
		};
	}

	template<class T> String BasicStringView<T>::join(const ArrayView<const StringView> strings) const {
		// Calculate size of the resulting string including delimiters
		const std::size_t delimiterSize = size();
		std::size_t totalSize = strings.empty() ? 0 : (strings.size() - 1) * delimiterSize;
		for (const StringView s : strings) totalSize += s.size();

		// Reserve memory for the resulting string
		String result{NoInit, totalSize};

		// Join strings
		char* out = result.data();
		char* const end = out + totalSize;
		for (const StringView string : strings) {
			const std::size_t stringSize = string.size();
			// Apparently memcpy() can't be called with null pointers, even if size is zero. I call that bullying.
			if (stringSize != 0) {
				std::memcpy(out, string._data, stringSize);
				out += stringSize;
			}
			if (delimiterSize != 0 && out != end) {
				std::memcpy(out, _data, delimiterSize);
				out += delimiterSize;
			}
		}

		return result;
	}

	template<class T> String BasicStringView<T>::join(const std::initializer_list<StringView> strings) const {
		return join(arrayView(strings));
	}

	template<class T> String BasicStringView<T>::joinWithoutEmptyParts(const ArrayView<const StringView> strings) const {
		// Calculate size of the resulting string including delimiters
		const std::size_t delimiterSize = size();
		std::size_t totalSize = 0;
		for (const StringView string : strings) {
			if (string.empty()) continue;
			totalSize += string.size() + delimiterSize;
		}
		if (totalSize != 0) totalSize -= delimiterSize;

		// Reserve memory for the resulting string
		String result{NoInit, totalSize};

		// Join strings
		char* out = result.data();
		char* const end = out + totalSize;
		for (const StringView string : strings) {
			if (string.empty()) continue;

			const std::size_t stringSize = string.size();
			// Apparently memcpy() can't be called with null pointers, even if size is zero. I call that bullying.
			if (stringSize != 0) {
				std::memcpy(out, string._data, stringSize);
				out += stringSize;
			}
			if (delimiterSize != 0 && out != end) {
				std::memcpy(out, _data, delimiterSize);
				out += delimiterSize;
			}
		}

		return result;
	}

	template<class T> String BasicStringView<T>::joinWithoutEmptyParts(const std::initializer_list<StringView> strings) const {
		return joinWithoutEmptyParts(arrayView(strings));
	}

	template<class T> bool BasicStringView<T>::hasPrefix(const StringView prefix) const {
		const std::size_t prefixSize = prefix.size();
		if (size() < prefixSize) return false;

		return std::memcmp(_data, prefix._data, prefixSize) == 0;
	}

	template<class T> bool BasicStringView<T>::hasPrefix(const char prefix) const {
		const std::size_t size = this->size();
		return size && _data[0] == prefix;
	}

	template<class T> bool BasicStringView<T>::hasSuffix(const StringView suffix) const {
		const std::size_t size = this->size();
		const std::size_t suffixSize = suffix.size();
		if (size < suffixSize) return false;

		return std::memcmp(_data + size - suffixSize, suffix._data, suffixSize) == 0;
	}

	template<class T> bool BasicStringView<T>::hasSuffix(const char suffix) const {
		const std::size_t size = this->size();
		return size != 0 && _data[size - 1] == suffix;
	}

	template<class T> BasicStringView<T> BasicStringView<T>::exceptPrefix(const StringView prefix) const {
		// Stripping a hardcoded prefix is unlikely to be called in a tight loop -- and the main purpose of this API is this
		// check -- so it shouldn't be a debug assert
		DEATH_ASSERT(hasPrefix(prefix), {}, "Containers::StringView::exceptPrefix(): String doesn't begin with specified prefix");
		return exceptPrefix(prefix.size());
	}

	template<class T> BasicStringView<T> BasicStringView<T>::exceptSuffix(const StringView suffix) const {
		// Stripping a hardcoded suffix is unlikely to be called in a tight loop -- and the main purpose of this API is this
		// check -- so it shouldn't be a debug assert
		DEATH_ASSERT(hasSuffix(suffix), {}, "Containers::StringView::exceptSuffix(): String doesn't end with specified suffix");
		return exceptSuffix(suffix.size());
	}

	template<class T> BasicStringView<T> BasicStringView<T>::trimmed() const {
#if !defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_CLANG_CL) || _MSC_VER >= 1930 /* MSVC 2022 works */
		return trimmed(Whitespace);
#else
		using namespace Literals;
		return trimmed(WHITESPACE_MACRO_BECAUSE_MSVC_IS_STUPID);
#endif
	}

	template<class T> BasicStringView<T> BasicStringView<T>::trimmedPrefix() const {
#if !defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_CLANG_CL) || _MSC_VER >= 1930 /* MSVC 2022 works */
		return trimmedPrefix(Whitespace);
#else
		using namespace Literals;
		return trimmedPrefix(WHITESPACE_MACRO_BECAUSE_MSVC_IS_STUPID);
#endif
	}

	template<class T> BasicStringView<T> BasicStringView<T>::trimmedSuffix() const {
#if !defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_CLANG_CL) || _MSC_VER >= 1930 /* MSVC 2022 works */
		return trimmedSuffix(Whitespace);
#else
		using namespace Literals;
		return trimmedSuffix(WHITESPACE_MACRO_BECAUSE_MSVC_IS_STUPID);
#endif
	}

	template class BasicStringView<char>;
	template class BasicStringView<const char>;

	bool operator==(const StringView a, const StringView b) {
		// Not using the size() accessor to speed up debug builds
		const std::size_t aSize = a._sizePlusFlags & ~Implementation::StringViewSizeMask;
		return aSize == (b._sizePlusFlags & ~Implementation::StringViewSizeMask) &&
			std::memcmp(a._data, b._data, aSize) == 0;
	}

	bool operator!=(const StringView a, const StringView b) {
		// Not using the size() accessor to speed up debug builds
		const std::size_t aSize = a._sizePlusFlags & ~Implementation::StringViewSizeMask;
		return aSize != (b._sizePlusFlags & ~Implementation::StringViewSizeMask) ||
			std::memcmp(a._data, b._data, aSize) != 0;
	}

	bool operator<(const StringView a, const StringView b) {
		// Not using the size() accessor to speed up debug builds
		const std::size_t aSize = a._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const std::size_t bSize = b._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const int result = std::memcmp(a._data, b._data, std::min(aSize, bSize));
		if (result != 0) return result < 0;
		if (aSize < bSize) return true;
		return false;
	}

	bool operator<=(const StringView a, const StringView b) {
		// Not using the size() accessor to speed up debug builds
		const std::size_t aSize = a._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const std::size_t bSize = b._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const int result = std::memcmp(a._data, b._data, std::min(aSize, bSize));
		if (result != 0) return result < 0;
		if (aSize <= bSize) return true;
		return false;
	}

	bool operator>=(const StringView a, const StringView b) {
		// Not using the size() accessor to speed up debug builds
		const std::size_t aSize = a._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const std::size_t bSize = b._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const int result = std::memcmp(a._data, b._data, std::min(aSize, bSize));
		if (result != 0) return result > 0;
		if (aSize >= bSize) return true;
		return false;
	}

	bool operator>(const StringView a, const StringView b) {
		// Not using the size() accessor to speed up debug builds
		const std::size_t aSize = a._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const std::size_t bSize = b._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const int result = std::memcmp(a._data, b._data, std::min(aSize, bSize));
		if (result != 0) return result > 0;
		if (aSize > bSize) return true;
		return false;
	}

	String operator*(const StringView string, const std::size_t count) {
		// Not using the size() accessor to speed up debug builds
		const std::size_t size = string._sizePlusFlags & ~Implementation::StringViewSizeMask;

		String result{NoInit, size * count};

		// Apparently memcpy() can't be called with null pointers, even if size is zero. I call that bullying.
		char* out = result.data();
		if (size != 0) for (std::size_t i = 0; i != count; ++i)
			std::memcpy(out + i * size, string._data, size);

		return result;
	}

	String operator*(const std::size_t count, const StringView string) {
		return string * count;
	}

	namespace Implementation
	{
		ArrayView<char> ArrayViewConverter<char, BasicStringView<char>>::from(const BasicStringView<char>& other) {
			return { other.data(), other.size() };
		}
		ArrayView<const char> ArrayViewConverter<const char, BasicStringView<char>>::from(const BasicStringView<char>& other) {
			return { other.data(), other.size() };
		}
		ArrayView<const char> ArrayViewConverter<const char, BasicStringView<const char>>::from(const BasicStringView<const char>& other) {
			return { other.data(), other.size() };
		}
	}

}}