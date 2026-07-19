#if defined(WITH_RHI_SOFTWARE)

#include "SwRaster.h"
#include "SwTileRenderer.h"
#include "SwScanlineOps.h"

#include <Cpu.h>
#if defined(DEATH_TARGET_X86)
#	if defined(DEATH_ENABLE_AVX2)
#		include <IntrinsicsAvx.h>
#	elif defined(DEATH_ENABLE_SSE2)
#		include <IntrinsicsSse2.h>
#	endif
#elif defined(DEATH_TARGET_ARM)
#	if defined(DEATH_ENABLE_NEON)
#		include <arm_neon.h>
#	endif
#elif defined(DEATH_TARGET_WASM)
#	if defined(DEATH_ENABLE_SIMD128)
#		include <wasm_simd128.h>
#	endif
#endif

#include <Containers/SmallVector.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>

using namespace Death;
using namespace Death::Containers;

namespace nCine::RhiSoftware
{
	// The sampler/filter enums live in the parent nCine namespace; pull them in so the ported rasterizer
	// bodies can name them unqualified (this TU never sees the SwRasterizer legacy shadow of the same names)
	using nCine::SamplerFilter;
	using nCine::SamplerWrapping;

	// =========================================================================
	// Persistent SW render state
	// =========================================================================
	namespace
	{
		struct SWState
		{
			// Active destination color buffer (owned by the device, RGBA8, width*height*4 bytes)
			std::uint8_t* colorBuffer = nullptr;
			std::int32_t  bufferWidth = 0;
			std::int32_t  bufferHeight = 0;
			// When true the buffer is a render-target texture and rows are written bottom-up (GL convention)
			bool          isFboTarget = false;

			bool          blendingEnabled = false;
			SwBlendFactor blendSrc = SwBlendFactor::SrcAlpha;
			SwBlendFactor blendDst = SwBlendFactor::OneMinusSrcAlpha;

			bool          scissorEnabled = false;
			std::int32_t  scissorX = 0;
			std::int32_t  scissorY = 0;
			std::int32_t  scissorW = 0;
			std::int32_t  scissorH = 0;

			// Viewport, used to map clip space to destination pixels in FetchVertex
			std::int32_t  viewportX = 0;
			std::int32_t  viewportY = 0;
			std::int32_t  viewportW = 0;
			std::int32_t  viewportH = 0;

			// Active draw context for the current draw call
			const DrawContext* drawCtx = nullptr;
		};

		SWState g_state;
	}

	namespace
	{
		// =====================================================================
		// SIMD-dispatched scanline blending (SrcAlpha / OneMinusSrcAlpha)
		// =====================================================================
		extern void DEATH_CPU_DISPATCHED_DECLARATION(blendScanlineSrcAlpha)(std::uint8_t* DEATH_RESTRICT dst, const std::uint8_t* DEATH_RESTRICT src, std::int32_t count);
		DEATH_CPU_DISPATCHER_DECLARATION(blendScanlineSrcAlpha)

		// Scalar fallback
		DEATH_CPU_MAYBE_UNUSED typename std::decay<decltype(blendScanlineSrcAlpha)>::type blendScanlineSrcAlphaImplementation(Cpu::ScalarT) {
			return [](std::uint8_t* DEATH_RESTRICT dst, const std::uint8_t* DEATH_RESTRICT src, std::int32_t count) {
				for (std::int32_t i = 0; i < count; ++i, dst += 4, src += 4) {
					const std::int32_t sA = src[3];
					if (sA == 0) continue;
					if (sA >= 255) {
						std::memcpy(dst, src, 4);
						continue;
					}
					const std::int32_t inv = 255 - sA;
					dst[0] = static_cast<std::uint8_t>((src[0] * sA + dst[0] * inv) >> 8);
					dst[1] = static_cast<std::uint8_t>((src[1] * sA + dst[1] * inv) >> 8);
					dst[2] = static_cast<std::uint8_t>((src[2] * sA + dst[2] * inv) >> 8);
					dst[3] = static_cast<std::uint8_t>((sA * 255 + dst[3] * inv) >> 8);
				}
			};
		}

#if defined(DEATH_ENABLE_SSE2)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_SSE2 typename std::decay<decltype(blendScanlineSrcAlpha)>::type blendScanlineSrcAlphaImplementation(Cpu::Sse2T) {
			return [](std::uint8_t* DEATH_RESTRICT dst, const std::uint8_t* DEATH_RESTRICT src, std::int32_t count) DEATH_ENABLE_SSE2 {
				const __m128i zero = _mm_setzero_si128();
				const __m128i c255 = _mm_set1_epi16(255);

				std::int32_t i = 0;
				for (; i + 4 <= count; i += 4, dst += 16, src += 16) {
					__m128i srcPx = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
					__m128i dstPx = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst));

					// Low 2 pixels: unpack bytes to 16-bit
					__m128i sLo = _mm_unpacklo_epi8(srcPx, zero);
					__m128i dLo = _mm_unpacklo_epi8(dstPx, zero);
					// Broadcast alpha of each pixel: word[3]→[0..3], word[7]→[4..7]
					__m128i aLo = _mm_shufflelo_epi16(sLo, _MM_SHUFFLE(3, 3, 3, 3));
					aLo = _mm_shufflehi_epi16(aLo, _MM_SHUFFLE(3, 3, 3, 3));
					__m128i iaLo = _mm_sub_epi16(c255, aLo);
					__m128i rLo = _mm_add_epi16(_mm_mullo_epi16(sLo, aLo), _mm_mullo_epi16(dLo, iaLo));
					rLo = _mm_srli_epi16(rLo, 8);

					// High 2 pixels
					__m128i sHi = _mm_unpackhi_epi8(srcPx, zero);
					__m128i dHi = _mm_unpackhi_epi8(dstPx, zero);
					__m128i aHi = _mm_shufflelo_epi16(sHi, _MM_SHUFFLE(3, 3, 3, 3));
					aHi = _mm_shufflehi_epi16(aHi, _MM_SHUFFLE(3, 3, 3, 3));
					__m128i iaHi = _mm_sub_epi16(c255, aHi);
					__m128i rHi = _mm_add_epi16(_mm_mullo_epi16(sHi, aHi), _mm_mullo_epi16(dHi, iaHi));
					rHi = _mm_srli_epi16(rHi, 8);

					_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), _mm_packus_epi16(rLo, rHi));
				}
				// Scalar tail
				for (; i < count; ++i, dst += 4, src += 4) {
					const std::int32_t sA = src[3];
					if (sA == 0) continue;
					if (sA >= 255) { std::memcpy(dst, src, 4); continue; }
					const std::int32_t inv = 255 - sA;
					dst[0] = static_cast<std::uint8_t>((src[0] * sA + dst[0] * inv) >> 8);
					dst[1] = static_cast<std::uint8_t>((src[1] * sA + dst[1] * inv) >> 8);
					dst[2] = static_cast<std::uint8_t>((src[2] * sA + dst[2] * inv) >> 8);
					dst[3] = static_cast<std::uint8_t>((sA * 255 + dst[3] * inv) >> 8);
				}
			};
		}
#endif

#if defined(DEATH_ENABLE_AVX2)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_AVX2 typename std::decay<decltype(blendScanlineSrcAlpha)>::type blendScanlineSrcAlphaImplementation(Cpu::Avx2T) {
			return [](std::uint8_t* DEATH_RESTRICT dst, const std::uint8_t* DEATH_RESTRICT src, std::int32_t count) DEATH_ENABLE_AVX2 {
				const __m256i zero = _mm256_setzero_si256();
				const __m256i c255 = _mm256_set1_epi16(255);

				std::int32_t i = 0;
				for (; i + 8 <= count; i += 8, dst += 32, src += 32) {
					__m256i srcPx = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src));
					__m256i dstPx = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(dst));

					// Within each 128-bit lane: unpacklo processes first 2 pixels, unpackhi the next 2
					__m256i sLo = _mm256_unpacklo_epi8(srcPx, zero);
					__m256i dLo = _mm256_unpacklo_epi8(dstPx, zero);
					__m256i aLo = _mm256_shufflelo_epi16(sLo, _MM_SHUFFLE(3, 3, 3, 3));
					aLo = _mm256_shufflehi_epi16(aLo, _MM_SHUFFLE(3, 3, 3, 3));
					__m256i iaLo = _mm256_sub_epi16(c255, aLo);
					__m256i rLo = _mm256_add_epi16(_mm256_mullo_epi16(sLo, aLo), _mm256_mullo_epi16(dLo, iaLo));
					rLo = _mm256_srli_epi16(rLo, 8);

					__m256i sHi = _mm256_unpackhi_epi8(srcPx, zero);
					__m256i dHi = _mm256_unpackhi_epi8(dstPx, zero);
					__m256i aHi = _mm256_shufflelo_epi16(sHi, _MM_SHUFFLE(3, 3, 3, 3));
					aHi = _mm256_shufflehi_epi16(aHi, _MM_SHUFFLE(3, 3, 3, 3));
					__m256i iaHi = _mm256_sub_epi16(c255, aHi);
					__m256i rHi = _mm256_add_epi16(_mm256_mullo_epi16(sHi, aHi), _mm256_mullo_epi16(dHi, iaHi));
					rHi = _mm256_srli_epi16(rHi, 8);

					_mm256_storeu_si256(reinterpret_cast<__m256i*>(dst), _mm256_packus_epi16(rLo, rHi));
				}
				// SSE2 tail (4 pixels at a time)
				const __m128i zero128 = _mm_setzero_si128();
				const __m128i c255_128 = _mm_set1_epi16(255);
				for (; i + 4 <= count; i += 4, dst += 16, src += 16) {
					__m128i srcPx = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
					__m128i dstPx = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst));
					__m128i sLo = _mm_unpacklo_epi8(srcPx, zero128);
					__m128i dLo = _mm_unpacklo_epi8(dstPx, zero128);
					__m128i aLo = _mm_shufflelo_epi16(sLo, _MM_SHUFFLE(3, 3, 3, 3));
					aLo = _mm_shufflehi_epi16(aLo, _MM_SHUFFLE(3, 3, 3, 3));
					__m128i iaLo = _mm_sub_epi16(c255_128, aLo);
					__m128i rLo = _mm_add_epi16(_mm_mullo_epi16(sLo, aLo), _mm_mullo_epi16(dLo, iaLo));
					rLo = _mm_srli_epi16(rLo, 8);
					__m128i sHi = _mm_unpackhi_epi8(srcPx, zero128);
					__m128i dHi = _mm_unpackhi_epi8(dstPx, zero128);
					__m128i aHi = _mm_shufflelo_epi16(sHi, _MM_SHUFFLE(3, 3, 3, 3));
					aHi = _mm_shufflehi_epi16(aHi, _MM_SHUFFLE(3, 3, 3, 3));
					__m128i iaHi = _mm_sub_epi16(c255_128, aHi);
					__m128i rHi = _mm_add_epi16(_mm_mullo_epi16(sHi, aHi), _mm_mullo_epi16(dHi, iaHi));
					rHi = _mm_srli_epi16(rHi, 8);
					_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), _mm_packus_epi16(rLo, rHi));
				}
				// Scalar tail
				for (; i < count; ++i, dst += 4, src += 4) {
					const std::int32_t sA = src[3];
					if (sA == 0) continue;
					if (sA >= 255) { std::memcpy(dst, src, 4); continue; }
					const std::int32_t inv = 255 - sA;
					dst[0] = static_cast<std::uint8_t>((src[0] * sA + dst[0] * inv) >> 8);
					dst[1] = static_cast<std::uint8_t>((src[1] * sA + dst[1] * inv) >> 8);
					dst[2] = static_cast<std::uint8_t>((src[2] * sA + dst[2] * inv) >> 8);
					dst[3] = static_cast<std::uint8_t>((sA * 255 + dst[3] * inv) >> 8);
				}
			};
		}
#endif

#if defined(DEATH_ENABLE_NEON)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_NEON typename std::decay<decltype(blendScanlineSrcAlpha)>::type blendScanlineSrcAlphaImplementation(Cpu::NeonT) {
			return [](std::uint8_t* DEATH_RESTRICT dst, const std::uint8_t* DEATH_RESTRICT src, std::int32_t count) DEATH_ENABLE_NEON {
				static const std::uint8_t alphaIdxData[8] = { 3, 3, 3, 3, 7, 7, 7, 7 };
				const uint8x8_t alphaIdx = vld1_u8(alphaIdxData);
				const uint8x8_t c255x8 = vdup_n_u8(255);

				std::int32_t i = 0;
				for (; i + 4 <= count; i += 4, dst += 16, src += 16) {
					uint8x16_t srcPx = vld1q_u8(src);
					uint8x16_t dstPx = vld1q_u8(dst);

					// Low 2 pixels
					uint8x8_t srcLo = vget_low_u8(srcPx);
					uint8x8_t dstLo = vget_low_u8(dstPx);
					uint8x8_t aLo = vtbl1_u8(srcLo, alphaIdx);
					uint8x8_t iaLo = vsub_u8(c255x8, aLo);
					uint16x8_t rLo = vaddq_u16(vmull_u8(srcLo, aLo), vmull_u8(dstLo, iaLo));

					// High 2 pixels
					uint8x8_t srcHi = vget_high_u8(srcPx);
					uint8x8_t dstHi = vget_high_u8(dstPx);
					uint8x8_t aHi = vtbl1_u8(srcHi, alphaIdx);
					uint8x8_t iaHi = vsub_u8(c255x8, aHi);
					uint16x8_t rHi = vaddq_u16(vmull_u8(srcHi, aHi), vmull_u8(dstHi, iaHi));

					vst1q_u8(dst, vcombine_u8(vshrn_n_u16(rLo, 8), vshrn_n_u16(rHi, 8)));
				}
				// Scalar tail
				for (; i < count; ++i, dst += 4, src += 4) {
					const std::int32_t sA = src[3];
					if (sA == 0) continue;
					if (sA >= 255) { std::memcpy(dst, src, 4); continue; }
					const std::int32_t inv = 255 - sA;
					dst[0] = static_cast<std::uint8_t>((src[0] * sA + dst[0] * inv) >> 8);
					dst[1] = static_cast<std::uint8_t>((src[1] * sA + dst[1] * inv) >> 8);
					dst[2] = static_cast<std::uint8_t>((src[2] * sA + dst[2] * inv) >> 8);
					dst[3] = static_cast<std::uint8_t>((sA * 255 + dst[3] * inv) >> 8);
				}
			};
		}
#endif

#if defined(DEATH_ENABLE_SIMD128)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_SIMD128 typename std::decay<decltype(blendScanlineSrcAlpha)>::type blendScanlineSrcAlphaImplementation(Cpu::Simd128T) {
			return [](std::uint8_t* DEATH_RESTRICT dst, const std::uint8_t* DEATH_RESTRICT src, std::int32_t count) DEATH_ENABLE_SIMD128 {
				const v128_t c255 = wasm_i16x8_splat(255);

				std::int32_t i = 0;
				for (; i + 4 <= count; i += 4, dst += 16, src += 16) {
					v128_t srcPx = wasm_v128_load(src);
					v128_t dstPx = wasm_v128_load(dst);

					// Low 2 pixels: extend to 16-bit
					v128_t sLo = wasm_u16x8_extend_low_u8x16(srcPx);
					v128_t dLo = wasm_u16x8_extend_low_u8x16(dstPx);
					// Broadcast alpha: byte shuffle on 16-bit data
					v128_t aLo = wasm_i8x16_shuffle(sLo, sLo, 6, 7, 6, 7, 6, 7, 6, 7, 14, 15, 14, 15, 14, 15, 14, 15);
					v128_t iaLo = wasm_i16x8_sub(c255, aLo);
					v128_t rLo = wasm_u16x8_shr(wasm_i16x8_add(wasm_i16x8_mul(sLo, aLo), wasm_i16x8_mul(dLo, iaLo)), 8);

					// High 2 pixels
					v128_t sHi = wasm_u16x8_extend_high_u8x16(srcPx);
					v128_t dHi = wasm_u16x8_extend_high_u8x16(dstPx);
					v128_t aHi = wasm_i8x16_shuffle(sHi, sHi, 6, 7, 6, 7, 6, 7, 6, 7, 14, 15, 14, 15, 14, 15, 14, 15);
					v128_t iaHi = wasm_i16x8_sub(c255, aHi);
					v128_t rHi = wasm_u16x8_shr(wasm_i16x8_add(wasm_i16x8_mul(sHi, aHi), wasm_i16x8_mul(dHi, iaHi)), 8);

					wasm_v128_store(dst, wasm_u8x16_narrow_i16x8(rLo, rHi));
				}
				// Scalar tail
				for (; i < count; ++i, dst += 4, src += 4) {
					const std::int32_t sA = src[3];
					if (sA == 0) continue;
					if (sA >= 255) { std::memcpy(dst, src, 4); continue; }
					const std::int32_t inv = 255 - sA;
					dst[0] = static_cast<std::uint8_t>((src[0] * sA + dst[0] * inv) >> 8);
					dst[1] = static_cast<std::uint8_t>((src[1] * sA + dst[1] * inv) >> 8);
					dst[2] = static_cast<std::uint8_t>((src[2] * sA + dst[2] * inv) >> 8);
					dst[3] = static_cast<std::uint8_t>((sA * 255 + dst[3] * inv) >> 8);
				}
			};
		}
#endif

		DEATH_CPU_DISPATCHER_BASE(blendScanlineSrcAlphaImplementation)
		DEATH_CPU_DISPATCHED(blendScanlineSrcAlphaImplementation, void DEATH_CPU_DISPATCHED_DECLARATION(blendScanlineSrcAlpha)(std::uint8_t* DEATH_RESTRICT dst, const std::uint8_t* DEATH_RESTRICT src, std::int32_t count))({
			return blendScanlineSrcAlphaImplementation(Cpu::DefaultBase)(dst, src, count);
		})

		// =====================================================================
		// CPU-dispatched scanline tint (multiply RGBA by constant color)
		// =====================================================================
		extern void DEATH_CPU_DISPATCHED_DECLARATION(tintScanline)(std::uint8_t* DEATH_RESTRICT buf, std::int32_t count, std::int32_t tR, std::int32_t tG, std::int32_t tB, std::int32_t tA);
		DEATH_CPU_DISPATCHER_DECLARATION(tintScanline)

		// Scalar fallback
		DEATH_CPU_MAYBE_UNUSED typename std::decay<decltype(tintScanline)>::type tintScanlineImplementation(Cpu::ScalarT) {
			return [](std::uint8_t* DEATH_RESTRICT buf, std::int32_t count, std::int32_t tR, std::int32_t tG, std::int32_t tB, std::int32_t tA) {
				for (std::int32_t i = 0; i < count; ++i, buf += 4) {
					buf[0] = static_cast<std::uint8_t>((buf[0] * tR) >> 8);
					buf[1] = static_cast<std::uint8_t>((buf[1] * tG) >> 8);
					buf[2] = static_cast<std::uint8_t>((buf[2] * tB) >> 8);
					buf[3] = static_cast<std::uint8_t>((buf[3] * tA) >> 8);
				}
			};
		}

#if defined(DEATH_ENABLE_SSE2)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_SSE2 typename std::decay<decltype(tintScanline)>::type tintScanlineImplementation(Cpu::Sse2T) {
			return [](std::uint8_t* DEATH_RESTRICT buf, std::int32_t count, std::int32_t tR, std::int32_t tG, std::int32_t tB, std::int32_t tA) DEATH_ENABLE_SSE2 {
				const __m128i zero = _mm_setzero_si128();
				// Pack tint as 16-bit multipliers: [tR, tG, tB, tA, tR, tG, tB, tA]
				const __m128i tint = _mm_set_epi16((std::int16_t)tA, (std::int16_t)tB, (std::int16_t)tG, (std::int16_t)tR,
				                                   (std::int16_t)tA, (std::int16_t)tB, (std::int16_t)tG, (std::int16_t)tR);

				std::int32_t i = 0;
				for (; i + 4 <= count; i += 4, buf += 16) {
					__m128i px = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf));

					// Process low 2 pixels
					__m128i lo = _mm_unpacklo_epi8(px, zero);
					lo = _mm_srli_epi16(_mm_mullo_epi16(lo, tint), 8);

					// Process high 2 pixels
					__m128i hi = _mm_unpackhi_epi8(px, zero);
					hi = _mm_srli_epi16(_mm_mullo_epi16(hi, tint), 8);

					_mm_storeu_si128(reinterpret_cast<__m128i*>(buf), _mm_packus_epi16(lo, hi));
				}
				// Scalar tail
				for (; i < count; i++, buf += 4) {
					buf[0] = static_cast<std::uint8_t>((buf[0] * tR) >> 8);
					buf[1] = static_cast<std::uint8_t>((buf[1] * tG) >> 8);
					buf[2] = static_cast<std::uint8_t>((buf[2] * tB) >> 8);
					buf[3] = static_cast<std::uint8_t>((buf[3] * tA) >> 8);
				}
			};
		}
#endif

#if defined(DEATH_ENABLE_AVX2)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_AVX2 typename std::decay<decltype(tintScanline)>::type tintScanlineImplementation(Cpu::Avx2T) {
			return [](std::uint8_t* DEATH_RESTRICT buf, std::int32_t count, std::int32_t tR, std::int32_t tG, std::int32_t tB, std::int32_t tA) DEATH_ENABLE_AVX2 {
				const __m256i zero = _mm256_setzero_si256();
				const __m256i tint = _mm256_set_epi16(
					(std::int16_t)tA, (std::int16_t)tB, (std::int16_t)tG, (std::int16_t)tR,
					(std::int16_t)tA, (std::int16_t)tB, (std::int16_t)tG, (std::int16_t)tR,
					(std::int16_t)tA, (std::int16_t)tB, (std::int16_t)tG, (std::int16_t)tR,
					(std::int16_t)tA, (std::int16_t)tB, (std::int16_t)tG, (std::int16_t)tR);

				std::int32_t i = 0;
				for (; i + 8 <= count; i += 8, buf += 32) {
					__m256i px = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf));

					__m256i lo = _mm256_unpacklo_epi8(px, zero);
					lo = _mm256_srli_epi16(_mm256_mullo_epi16(lo, tint), 8);

					__m256i hi = _mm256_unpackhi_epi8(px, zero);
					hi = _mm256_srli_epi16(_mm256_mullo_epi16(hi, tint), 8);

					_mm256_storeu_si256(reinterpret_cast<__m256i*>(buf), _mm256_packus_epi16(lo, hi));
				}
				// SSE2 tail for remaining 4-pixel groups
				const __m128i tint128 = _mm_set_epi16((std::int16_t)tA, (std::int16_t)tB, (std::int16_t)tG, (std::int16_t)tR,
				                                      (std::int16_t)tA, (std::int16_t)tB, (std::int16_t)tG, (std::int16_t)tR);
				const __m128i zero128 = _mm_setzero_si128();
				for (; i + 4 <= count; i += 4, buf += 16) {
					__m128i px = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf));
					__m128i lo = _mm_srli_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(px, zero128), tint128), 8);
					__m128i hi = _mm_srli_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(px, zero128), tint128), 8);
					_mm_storeu_si128(reinterpret_cast<__m128i*>(buf), _mm_packus_epi16(lo, hi));
				}
				// Scalar tail
				for (; i < count; i++, buf += 4) {
					buf[0] = static_cast<std::uint8_t>((buf[0] * tR) >> 8);
					buf[1] = static_cast<std::uint8_t>((buf[1] * tG) >> 8);
					buf[2] = static_cast<std::uint8_t>((buf[2] * tB) >> 8);
					buf[3] = static_cast<std::uint8_t>((buf[3] * tA) >> 8);
				}
			};
		}
#endif

#if defined(DEATH_ENABLE_NEON)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_NEON typename std::decay<decltype(tintScanline)>::type tintScanlineImplementation(Cpu::NeonT) {
			return [](std::uint8_t* DEATH_RESTRICT buf, std::int32_t count, std::int32_t tR, std::int32_t tG, std::int32_t tB, std::int32_t tA) DEATH_ENABLE_NEON {
				const uint8x8_t vtR = vdup_n_u8((std::uint8_t)tR);
				const uint8x8_t vtG = vdup_n_u8((std::uint8_t)tG);
				const uint8x8_t vtB = vdup_n_u8((std::uint8_t)tB);
				const uint8x8_t vtA = vdup_n_u8((std::uint8_t)tA);

				std::int32_t i = 0;
				for (; i + 8 <= count; i += 8, buf += 32) {
					uint8x8x4_t px = vld4_u8(buf);
					// Multiply and shift: (val * tint) >> 8
					px.val[0] = vshrn_n_u16(vmull_u8(px.val[0], vtR), 8);
					px.val[1] = vshrn_n_u16(vmull_u8(px.val[1], vtG), 8);
					px.val[2] = vshrn_n_u16(vmull_u8(px.val[2], vtB), 8);
					px.val[3] = vshrn_n_u16(vmull_u8(px.val[3], vtA), 8);
					vst4_u8(buf, px);
				}
				// Scalar tail
				for (; i < count; ++i, buf += 4) {
					buf[0] = static_cast<std::uint8_t>((buf[0] * tR) >> 8);
					buf[1] = static_cast<std::uint8_t>((buf[1] * tG) >> 8);
					buf[2] = static_cast<std::uint8_t>((buf[2] * tB) >> 8);
					buf[3] = static_cast<std::uint8_t>((buf[3] * tA) >> 8);
				}
			};
		}
#endif

#if defined(DEATH_ENABLE_SIMD128)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_SIMD128 typename std::decay<decltype(tintScanline)>::type tintScanlineImplementation(Cpu::Simd128T) {
			return [](std::uint8_t* DEATH_RESTRICT buf, std::int32_t count, std::int32_t tR, std::int32_t tG, std::int32_t tB, std::int32_t tA) DEATH_ENABLE_SIMD128 {
				const v128_t zero = wasm_i32x4_splat(0);
				const v128_t tint = wasm_i16x8_make(tR, tG, tB, tA, tR, tG, tB, tA);

				std::int32_t i = 0;
				for (; i + 4 <= count; i += 4, buf += 16) {
					v128_t px = wasm_v128_load(buf);

					v128_t lo = wasm_i16x8_shr(wasm_i16x8_mul(wasm_u16x8_extend_low_u8x16(px), tint), 8);
					v128_t hi = wasm_i16x8_shr(wasm_i16x8_mul(wasm_u16x8_extend_high_u8x16(px), tint), 8);

					wasm_v128_store(buf, wasm_u8x16_narrow_i16x8(lo, hi));
				}
				// Scalar tail
				for (; i < count; ++i, buf += 4) {
					buf[0] = static_cast<std::uint8_t>((buf[0] * tR) >> 8);
					buf[1] = static_cast<std::uint8_t>((buf[1] * tG) >> 8);
					buf[2] = static_cast<std::uint8_t>((buf[2] * tB) >> 8);
					buf[3] = static_cast<std::uint8_t>((buf[3] * tA) >> 8);
				}
			};
		}
#endif

		DEATH_CPU_DISPATCHER_BASE(tintScanlineImplementation)
		DEATH_CPU_DISPATCHED(tintScanlineImplementation, void DEATH_CPU_DISPATCHED_DECLARATION(tintScanline)(std::uint8_t* DEATH_RESTRICT buf, std::int32_t count, std::int32_t tR, std::int32_t tG, std::int32_t tB, std::int32_t tA))({
			return tintScanlineImplementation(Cpu::DefaultBase)(buf, count, tR, tG, tB, tA);
		})

		// =====================================================================
		// CPU-dispatched constant-source blend (solid no-texture fills)
		// dst = (src * a + dst * (255 - a)) >> 8 per channel, alpha = (a * 255 + dst.a * (255 - a)) >> 8,
		// i.e. exactly the per-pixel fast-blend including its destination-alpha convention. The numerators
		// are per-call constants, so the SIMD forms are one multiply-add per 16-bit lane; every sum is a
		// convex combination bounded by 255 * 255, so it never overflows the 16-bit intermediate.
		// =====================================================================
		extern void DEATH_CPU_DISPATCHED_DECLARATION(blendScanlineConstSrcAlpha)(std::uint8_t* DEATH_RESTRICT dst, std::int32_t count, const std::uint8_t* DEATH_RESTRICT src);
		DEATH_CPU_DISPATCHER_DECLARATION(blendScanlineConstSrcAlpha)

		// Scalar fallback
		DEATH_CPU_MAYBE_UNUSED typename std::decay<decltype(blendScanlineConstSrcAlpha)>::type blendScanlineConstSrcAlphaImplementation(Cpu::ScalarT) {
			return [](std::uint8_t* DEATH_RESTRICT dst, std::int32_t count, const std::uint8_t* DEATH_RESTRICT src) {
				const std::int32_t sA = src[3];
				const std::int32_t inv = 255 - sA;
				const std::int32_t nR = src[0] * sA, nG = src[1] * sA, nB = src[2] * sA, nA = sA * 255;
				for (std::int32_t i = 0; i < count; ++i, dst += 4) {
					dst[0] = static_cast<std::uint8_t>((nR + dst[0] * inv) >> 8);
					dst[1] = static_cast<std::uint8_t>((nG + dst[1] * inv) >> 8);
					dst[2] = static_cast<std::uint8_t>((nB + dst[2] * inv) >> 8);
					dst[3] = static_cast<std::uint8_t>((nA + dst[3] * inv) >> 8);
				}
			};
		}

#if defined(DEATH_ENABLE_SSE2)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_SSE2 typename std::decay<decltype(blendScanlineConstSrcAlpha)>::type blendScanlineConstSrcAlphaImplementation(Cpu::Sse2T) {
			return [](std::uint8_t* DEATH_RESTRICT dst, std::int32_t count, const std::uint8_t* DEATH_RESTRICT src) DEATH_ENABLE_SSE2 {
				const std::int32_t sA = src[3];
				const std::int32_t inv = 255 - sA;
				const std::int32_t nR = src[0] * sA, nG = src[1] * sA, nB = src[2] * sA, nA = sA * 255;
				const __m128i zero = _mm_setzero_si128();
				const __m128i num = _mm_set_epi16((std::int16_t)nA, (std::int16_t)nB, (std::int16_t)nG, (std::int16_t)nR,
				                                  (std::int16_t)nA, (std::int16_t)nB, (std::int16_t)nG, (std::int16_t)nR);
				const __m128i vinv = _mm_set1_epi16((std::int16_t)inv);

				std::int32_t i = 0;
				for (; i + 4 <= count; i += 4, dst += 16) {
					__m128i dstPx = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst));
					__m128i lo = _mm_srli_epi16(_mm_add_epi16(num, _mm_mullo_epi16(_mm_unpacklo_epi8(dstPx, zero), vinv)), 8);
					__m128i hi = _mm_srli_epi16(_mm_add_epi16(num, _mm_mullo_epi16(_mm_unpackhi_epi8(dstPx, zero), vinv)), 8);
					_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), _mm_packus_epi16(lo, hi));
				}
				// Scalar tail
				for (; i < count; ++i, dst += 4) {
					dst[0] = static_cast<std::uint8_t>((nR + dst[0] * inv) >> 8);
					dst[1] = static_cast<std::uint8_t>((nG + dst[1] * inv) >> 8);
					dst[2] = static_cast<std::uint8_t>((nB + dst[2] * inv) >> 8);
					dst[3] = static_cast<std::uint8_t>((nA + dst[3] * inv) >> 8);
				}
			};
		}
#endif

#if defined(DEATH_ENABLE_AVX2)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_AVX2 typename std::decay<decltype(blendScanlineConstSrcAlpha)>::type blendScanlineConstSrcAlphaImplementation(Cpu::Avx2T) {
			return [](std::uint8_t* DEATH_RESTRICT dst, std::int32_t count, const std::uint8_t* DEATH_RESTRICT src) DEATH_ENABLE_AVX2 {
				const std::int32_t sA = src[3];
				const std::int32_t inv = 255 - sA;
				const std::int32_t nR = src[0] * sA, nG = src[1] * sA, nB = src[2] * sA, nA = sA * 255;
				const __m256i zero = _mm256_setzero_si256();
				const __m256i num = _mm256_set_epi16(
					(std::int16_t)nA, (std::int16_t)nB, (std::int16_t)nG, (std::int16_t)nR,
					(std::int16_t)nA, (std::int16_t)nB, (std::int16_t)nG, (std::int16_t)nR,
					(std::int16_t)nA, (std::int16_t)nB, (std::int16_t)nG, (std::int16_t)nR,
					(std::int16_t)nA, (std::int16_t)nB, (std::int16_t)nG, (std::int16_t)nR);
				const __m256i vinv = _mm256_set1_epi16((std::int16_t)inv);

				std::int32_t i = 0;
				for (; i + 8 <= count; i += 8, dst += 32) {
					__m256i dstPx = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(dst));
					__m256i lo = _mm256_srli_epi16(_mm256_add_epi16(num, _mm256_mullo_epi16(_mm256_unpacklo_epi8(dstPx, zero), vinv)), 8);
					__m256i hi = _mm256_srli_epi16(_mm256_add_epi16(num, _mm256_mullo_epi16(_mm256_unpackhi_epi8(dstPx, zero), vinv)), 8);
					_mm256_storeu_si256(reinterpret_cast<__m256i*>(dst), _mm256_packus_epi16(lo, hi));
				}
				// SSE2 tail (4 pixels at a time)
				const __m128i zero128 = _mm_setzero_si128();
				const __m128i num128 = _mm_set_epi16((std::int16_t)nA, (std::int16_t)nB, (std::int16_t)nG, (std::int16_t)nR,
				                                     (std::int16_t)nA, (std::int16_t)nB, (std::int16_t)nG, (std::int16_t)nR);
				const __m128i vinv128 = _mm_set1_epi16((std::int16_t)inv);
				for (; i + 4 <= count; i += 4, dst += 16) {
					__m128i dstPx = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst));
					__m128i lo = _mm_srli_epi16(_mm_add_epi16(num128, _mm_mullo_epi16(_mm_unpacklo_epi8(dstPx, zero128), vinv128)), 8);
					__m128i hi = _mm_srli_epi16(_mm_add_epi16(num128, _mm_mullo_epi16(_mm_unpackhi_epi8(dstPx, zero128), vinv128)), 8);
					_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), _mm_packus_epi16(lo, hi));
				}
				// Scalar tail
				for (; i < count; ++i, dst += 4) {
					dst[0] = static_cast<std::uint8_t>((nR + dst[0] * inv) >> 8);
					dst[1] = static_cast<std::uint8_t>((nG + dst[1] * inv) >> 8);
					dst[2] = static_cast<std::uint8_t>((nB + dst[2] * inv) >> 8);
					dst[3] = static_cast<std::uint8_t>((nA + dst[3] * inv) >> 8);
				}
			};
		}
#endif

#if defined(DEATH_ENABLE_NEON)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_NEON typename std::decay<decltype(blendScanlineConstSrcAlpha)>::type blendScanlineConstSrcAlphaImplementation(Cpu::NeonT) {
			return [](std::uint8_t* DEATH_RESTRICT dst, std::int32_t count, const std::uint8_t* DEATH_RESTRICT src) DEATH_ENABLE_NEON {
				const std::int32_t sA = src[3];
				const std::int32_t inv = 255 - sA;
				const std::int32_t nR = src[0] * sA, nG = src[1] * sA, nB = src[2] * sA, nA = sA * 255;
				const std::uint16_t numArr[8] = {
					(std::uint16_t)nR, (std::uint16_t)nG, (std::uint16_t)nB, (std::uint16_t)nA,
					(std::uint16_t)nR, (std::uint16_t)nG, (std::uint16_t)nB, (std::uint16_t)nA
				};
				const uint16x8_t num = vld1q_u16(numArr);
				const uint8x8_t vinv = vdup_n_u8((std::uint8_t)inv);

				std::int32_t i = 0;
				for (; i + 4 <= count; i += 4, dst += 16) {
					uint8x16_t dstPx = vld1q_u8(dst);
					uint16x8_t lo = vaddq_u16(num, vmull_u8(vget_low_u8(dstPx), vinv));
					uint16x8_t hi = vaddq_u16(num, vmull_u8(vget_high_u8(dstPx), vinv));
					vst1q_u8(dst, vcombine_u8(vshrn_n_u16(lo, 8), vshrn_n_u16(hi, 8)));
				}
				// Scalar tail
				for (; i < count; ++i, dst += 4) {
					dst[0] = static_cast<std::uint8_t>((nR + dst[0] * inv) >> 8);
					dst[1] = static_cast<std::uint8_t>((nG + dst[1] * inv) >> 8);
					dst[2] = static_cast<std::uint8_t>((nB + dst[2] * inv) >> 8);
					dst[3] = static_cast<std::uint8_t>((nA + dst[3] * inv) >> 8);
				}
			};
		}
#endif

#if defined(DEATH_ENABLE_SIMD128)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_SIMD128 typename std::decay<decltype(blendScanlineConstSrcAlpha)>::type blendScanlineConstSrcAlphaImplementation(Cpu::Simd128T) {
			return [](std::uint8_t* DEATH_RESTRICT dst, std::int32_t count, const std::uint8_t* DEATH_RESTRICT src) DEATH_ENABLE_SIMD128 {
				const std::int32_t sA = src[3];
				const std::int32_t inv = 255 - sA;
				const std::int32_t nR = src[0] * sA, nG = src[1] * sA, nB = src[2] * sA, nA = sA * 255;
				const v128_t num = wasm_i16x8_make((std::int16_t)nR, (std::int16_t)nG, (std::int16_t)nB, (std::int16_t)nA,
				                                   (std::int16_t)nR, (std::int16_t)nG, (std::int16_t)nB, (std::int16_t)nA);
				const v128_t vinv = wasm_i16x8_splat((std::int16_t)inv);

				std::int32_t i = 0;
				for (; i + 4 <= count; i += 4, dst += 16) {
					v128_t dstPx = wasm_v128_load(dst);
					v128_t lo = wasm_u16x8_shr(wasm_i16x8_add(num, wasm_i16x8_mul(wasm_u16x8_extend_low_u8x16(dstPx), vinv)), 8);
					v128_t hi = wasm_u16x8_shr(wasm_i16x8_add(num, wasm_i16x8_mul(wasm_u16x8_extend_high_u8x16(dstPx), vinv)), 8);
					wasm_v128_store(dst, wasm_u8x16_narrow_i16x8(lo, hi));
				}
				// Scalar tail
				for (; i < count; ++i, dst += 4) {
					dst[0] = static_cast<std::uint8_t>((nR + dst[0] * inv) >> 8);
					dst[1] = static_cast<std::uint8_t>((nG + dst[1] * inv) >> 8);
					dst[2] = static_cast<std::uint8_t>((nB + dst[2] * inv) >> 8);
					dst[3] = static_cast<std::uint8_t>((nA + dst[3] * inv) >> 8);
				}
			};
		}
#endif

		DEATH_CPU_DISPATCHER_BASE(blendScanlineConstSrcAlphaImplementation)
		DEATH_CPU_DISPATCHED(blendScanlineConstSrcAlphaImplementation, void DEATH_CPU_DISPATCHED_DECLARATION(blendScanlineConstSrcAlpha)(std::uint8_t* DEATH_RESTRICT dst, std::int32_t count, const std::uint8_t* DEATH_RESTRICT src))({
			return blendScanlineConstSrcAlphaImplementation(Cpu::DefaultBase)(dst, count, src);
		})

		// =====================================================================
		// CPU-dispatched dynamic-lighting combine row (see SwScanlineOps.h)
		// The SSE2 variant processes 4 pixels per step with the scalar float operations replayed in the
		// same order per 4-wide lane, so its output is bit-identical to the scalar loop: a fully lit lane
		// computes (px / 255) * 1 + 0 whose re-quantization is exact for every byte, and the alpha lane
		// is carried through the transpose untouched. Only a scalar fallback exists for NEON/WASM (the
		// combine runs on dark levels only; those targets keep the previous per-pixel cost).
		// =====================================================================
		extern void DEATH_CPU_DISPATCHED_DECLARATION(combineLightingScanline)(std::uint8_t* DEATH_RESTRICT px, std::int32_t width, const float* DEATH_RESTRICT lmRow, std::int32_t lmW, std::int32_t scale, float ambR, float ambG, float ambB);
		DEATH_CPU_DISPATCHER_DECLARATION(combineLightingScanline)

		// Scalar fallback: the exact per-pixel loop the device ran inline before
		DEATH_CPU_MAYBE_UNUSED typename std::decay<decltype(combineLightingScanline)>::type combineLightingScanlineImplementation(Cpu::ScalarT) {
			return [](std::uint8_t* DEATH_RESTRICT px, std::int32_t width, const float* DEATH_RESTRICT lmRow, std::int32_t lmW, std::int32_t scale, float ambR, float ambG, float ambB) {
				for (std::int32_t x = 0; x < width; x++, px += 4) {
					const std::int32_t lmX = std::min(x / scale, lmW - 1);
					const float r = std::clamp(lmRow[lmX * 2], 0.0f, 1.0f);
					const float g = std::clamp(lmRow[lmX * 2 + 1], 0.0f, 1.0f);
					const float darkT = std::clamp(1.0f - r, 0.0f, 1.0f);
					if (darkT <= 0.0f && g <= 0.0f) {
						continue; // Fully lit, no brightness core: leave the scene pixel as-is
					}
					const float lit = 1.0f + g;
					const float core = (g > 0.7f ? g - 0.7f : 0.0f);
					float cr = (px[0] / 255.0f) * lit + core;
					float cg = (px[1] / 255.0f) * lit + core;
					float cb = (px[2] / 255.0f) * lit + core;
					cr += (ambR - cr) * darkT;
					cg += (ambG - cg) * darkT;
					cb += (ambB - cb) * darkT;
					px[0] = (std::uint8_t)std::clamp(cr * 255.0f + 0.5f, 0.0f, 255.0f);
					px[1] = (std::uint8_t)std::clamp(cg * 255.0f + 0.5f, 0.0f, 255.0f);
					px[2] = (std::uint8_t)std::clamp(cb * 255.0f + 0.5f, 0.0f, 255.0f);
				}
			};
		}

#if defined(DEATH_ENABLE_SSE2)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_SSE2 typename std::decay<decltype(combineLightingScanline)>::type combineLightingScanlineImplementation(Cpu::Sse2T) {
			return [](std::uint8_t* DEATH_RESTRICT px, std::int32_t width, const float* DEATH_RESTRICT lmRow, std::int32_t lmW, std::int32_t scale, float ambR, float ambG, float ambB) DEATH_ENABLE_SSE2 {
				const __m128 zerof = _mm_setzero_ps();
				const __m128 onef = _mm_set1_ps(1.0f);
				const __m128 halff = _mm_set1_ps(0.5f);
				const __m128 c255f = _mm_set1_ps(255.0f);
				const __m128 coreT = _mm_set1_ps(0.7f);
				const __m128 vAmbR = _mm_set1_ps(ambR);
				const __m128 vAmbG = _mm_set1_ps(ambG);
				const __m128 vAmbB = _mm_set1_ps(ambB);
				const __m128i zeroi = _mm_setzero_si128();

				// x / scale collapses to a shift when the scale is a power of two (the compositor uses 2)
				std::int32_t shift = -1;
				if ((scale & (scale - 1)) == 0) {
					shift = 0;
					while ((1 << shift) < scale) shift++;
				}
				const std::int32_t lmLast = lmW - 1;

				std::int32_t x = 0;
				for (; x + 4 <= width; x += 4, px += 16) {
					std::int32_t i0, i1, i2, i3;
					if (shift >= 0) {
						i0 = (x) >> shift; i1 = (x + 1) >> shift; i2 = (x + 2) >> shift; i3 = (x + 3) >> shift;
					} else {
						i0 = (x) / scale; i1 = (x + 1) / scale; i2 = (x + 2) / scale; i3 = (x + 3) / scale;
					}
					if (i0 > lmLast) i0 = lmLast;
					if (i1 > lmLast) i1 = lmLast;
					if (i2 > lmLast) i2 = lmLast;
					if (i3 > lmLast) i3 = lmLast;

					__m128 r = _mm_setr_ps(lmRow[i0 * 2], lmRow[i1 * 2], lmRow[i2 * 2], lmRow[i3 * 2]);
					__m128 g = _mm_setr_ps(lmRow[i0 * 2 + 1], lmRow[i1 * 2 + 1], lmRow[i2 * 2 + 1], lmRow[i3 * 2 + 1]);
					r = _mm_min_ps(_mm_max_ps(r, zerof), onef);
					g = _mm_min_ps(_mm_max_ps(g, zerof), onef);
					const __m128 darkT = _mm_min_ps(_mm_max_ps(_mm_sub_ps(onef, r), zerof), onef);

					// Per-4-pixel early out (the block form of the scalar per-pixel fully-lit skip)
					const __m128 active = _mm_or_ps(_mm_cmpgt_ps(darkT, zerof), _mm_cmpgt_ps(g, zerof));
					if (_mm_movemask_ps(active) == 0) {
						continue;
					}

					const __m128 lit = _mm_add_ps(onef, g);
					const __m128 core = _mm_and_ps(_mm_sub_ps(g, coreT), _mm_cmpgt_ps(g, coreT));

					// Deinterleave the 4 RGBA8 pixels into per-channel float vectors
					const __m128i pix = _mm_loadu_si128(reinterpret_cast<const __m128i*>(px));
					const __m128i lo16 = _mm_unpacklo_epi8(pix, zeroi);
					const __m128i hi16 = _mm_unpackhi_epi8(pix, zeroi);
					__m128 p0 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(lo16, zeroi));
					__m128 p1 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(lo16, zeroi));
					__m128 p2 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(hi16, zeroi));
					__m128 p3 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(hi16, zeroi));
					_MM_TRANSPOSE4_PS(p0, p1, p2, p3); // p0 = R, p1 = G, p2 = B, p3 = A across the 4 pixels

					// (px / 255) * lit + core, then += (amb - c) * darkT, then quantize - same op order as scalar
					p0 = _mm_add_ps(_mm_mul_ps(_mm_div_ps(p0, c255f), lit), core);
					p0 = _mm_add_ps(p0, _mm_mul_ps(_mm_sub_ps(vAmbR, p0), darkT));
					p0 = _mm_min_ps(_mm_max_ps(_mm_add_ps(_mm_mul_ps(p0, c255f), halff), zerof), c255f);
					p1 = _mm_add_ps(_mm_mul_ps(_mm_div_ps(p1, c255f), lit), core);
					p1 = _mm_add_ps(p1, _mm_mul_ps(_mm_sub_ps(vAmbG, p1), darkT));
					p1 = _mm_min_ps(_mm_max_ps(_mm_add_ps(_mm_mul_ps(p1, c255f), halff), zerof), c255f);
					p2 = _mm_add_ps(_mm_mul_ps(_mm_div_ps(p2, c255f), lit), core);
					p2 = _mm_add_ps(p2, _mm_mul_ps(_mm_sub_ps(vAmbB, p2), darkT));
					p2 = _mm_min_ps(_mm_max_ps(_mm_add_ps(_mm_mul_ps(p2, c255f), halff), zerof), c255f);
					// p3 (alpha) is left as loaded; the truncating convert below restores the exact byte

					// Reinterleave and store (values are in [0, 255], so the signed 16-bit pack is lossless)
					_MM_TRANSPOSE4_PS(p0, p1, p2, p3);
					const __m128i q01 = _mm_packs_epi32(_mm_cvttps_epi32(p0), _mm_cvttps_epi32(p1));
					const __m128i q23 = _mm_packs_epi32(_mm_cvttps_epi32(p2), _mm_cvttps_epi32(p3));
					_mm_storeu_si128(reinterpret_cast<__m128i*>(px), _mm_packus_epi16(q01, q23));
				}
				// Scalar tail
				for (; x < width; x++, px += 4) {
					const std::int32_t lmX = std::min(x / scale, lmW - 1);
					const float r = std::clamp(lmRow[lmX * 2], 0.0f, 1.0f);
					const float g = std::clamp(lmRow[lmX * 2 + 1], 0.0f, 1.0f);
					const float darkT = std::clamp(1.0f - r, 0.0f, 1.0f);
					if (darkT <= 0.0f && g <= 0.0f) {
						continue;
					}
					const float lit = 1.0f + g;
					const float core = (g > 0.7f ? g - 0.7f : 0.0f);
					float cr = (px[0] / 255.0f) * lit + core;
					float cg = (px[1] / 255.0f) * lit + core;
					float cb = (px[2] / 255.0f) * lit + core;
					cr += (ambR - cr) * darkT;
					cg += (ambG - cg) * darkT;
					cb += (ambB - cb) * darkT;
					px[0] = (std::uint8_t)std::clamp(cr * 255.0f + 0.5f, 0.0f, 255.0f);
					px[1] = (std::uint8_t)std::clamp(cg * 255.0f + 0.5f, 0.0f, 255.0f);
					px[2] = (std::uint8_t)std::clamp(cb * 255.0f + 0.5f, 0.0f, 255.0f);
				}
			};
		}
#endif

		DEATH_CPU_DISPATCHER_BASE(combineLightingScanlineImplementation)
		DEATH_CPU_DISPATCHED(combineLightingScanlineImplementation, void DEATH_CPU_DISPATCHED_DECLARATION(combineLightingScanline)(std::uint8_t* DEATH_RESTRICT px, std::int32_t width, const float* DEATH_RESTRICT lmRow, std::int32_t lmW, std::int32_t scale, float ambR, float ambG, float ambB))({
			return combineLightingScanlineImplementation(Cpu::DefaultBase)(px, width, lmRow, lmW, scale, ambR, ambG, ambB);
		})
	}

	// Externally-visible entry points (see SwScanlineOps.h) so the tile rasterizer TU runs the exact same
	// CPU-dispatched implementations instead of keeping its own (previously scalar-on-x86) copies
	void BlendScanlineSrcAlpha(std::uint8_t* dst, const std::uint8_t* src, std::int32_t count)
	{
		blendScanlineSrcAlpha(dst, src, count);
	}

	void TintScanline(std::uint8_t* buf, std::int32_t count, std::int32_t tR, std::int32_t tG, std::int32_t tB, std::int32_t tA)
	{
		tintScanline(buf, count, tR, tG, tB, tA);
	}

	void BlendScanlineConstSrcAlpha(std::uint8_t* dst, std::int32_t count, const std::uint8_t src[4])
	{
		blendScanlineConstSrcAlpha(dst, count, src);
	}

	void CombineLightingScanline(std::uint8_t* px, std::int32_t width, const float* lmRow, std::int32_t lmW,
		std::int32_t scale, float ambR, float ambG, float ambB)
	{
		combineLightingScanline(px, width, lmRow, lmW, scale, ambR, ambG, ambB);
	}

	// =========================================================================
	// Rasterization helpers
	// =========================================================================
	namespace
	{
		// UV wrapping for float coordinates [0..1] — shared by both rasterizers
		inline float WrapUV(float t, SamplerWrapping mode)
		{
			switch (mode) {
				case SamplerWrapping::Repeat:
					t -= std::floor(t);
					return t;
				case SamplerWrapping::MirroredRepeat: {
					float f = std::floor(t);
					t -= f;
					if (static_cast<std::int32_t>(f) & 1) t = 1.0f - t;
					return t;
				}
				default: // ClampToEdge
					if (t < 0.0f) return 0.0f;
					if (t > 1.0f) return 1.0f;
					return t;
			}
		}

		// Fixed-point UV wrapping for the axis-aligned quad rasterizer.
		// coordFix is in 16.16 format representing pixel index (u * texW * 65536).
		// Returns pixel index in [0, texDim).
		inline std::int32_t WrapTexelFix(std::int32_t coordFix, std::int32_t texDim, SamplerWrapping mode)
		{
			std::int32_t idx = coordFix >> 16;
			switch (mode) {
				case SamplerWrapping::Repeat:
					idx = idx % texDim;
					if (idx < 0) idx += texDim;
					return idx;
				case SamplerWrapping::MirroredRepeat: {
					std::int32_t period = texDim * 2;
					idx = idx % period;
					if (idx < 0) idx += period;
					if (idx >= texDim) idx = period - 1 - idx;
					return idx;
				}
				default: // ClampToEdge
					if (idx < 0) return 0;
					if (idx >= texDim) return texDim - 1;
					return idx;
			}
		}

		// Normalize a fixed-point UV to [0, texDimFix) for Repeat wrapping.
		// This is called once per scanline to allow branch-based wrap per pixel.
		inline std::int32_t NormalizeRepeatFix(std::int32_t coordFix, std::int32_t texDimFix)
		{
			coordFix = coordFix % texDimFix;
			if (coordFix < 0) coordFix += texDimFix;
			return coordFix;
		}

		// Advance a Repeat-normalized fixed-point UV by one step.
		inline std::int32_t AdvanceRepeatFix(std::int32_t coordFix, std::int32_t step, std::int32_t texDimFix)
		{
			coordFix += step;
			if (coordFix >= texDimFix) coordFix -= texDimFix;
			else if (coordFix < 0) coordFix += texDimFix;
			return coordFix;
		}

		// Generic integer blend for arbitrary SwBlendFactor (used by triangle rasterizer)
		inline void BlendPixelGeneric(std::uint8_t* dst, std::int32_t sR, std::int32_t sG,
		                              std::int32_t sB, std::int32_t sA,
		                              SwBlendFactor sFactor, SwBlendFactor dFactor)
		{
			if (sFactor == SwBlendFactor::SrcAlpha && dFactor == SwBlendFactor::OneMinusSrcAlpha) {
				// Fast path: most common blend mode
				const std::int32_t inv = 255 - sA;
				dst[0] = static_cast<std::uint8_t>((sR * sA + dst[0] * inv) >> 8);
				dst[1] = static_cast<std::uint8_t>((sG * sA + dst[1] * inv) >> 8);
				dst[2] = static_cast<std::uint8_t>((sB * sA + dst[2] * inv) >> 8);
				dst[3] = static_cast<std::uint8_t>((sA * 255 + dst[3] * inv) >> 8);
				return;
			}
			// General fallback using float math
			const float srcRf = sR / 255.0f, srcGf = sG / 255.0f, srcBf = sB / 255.0f, srcAf = sA / 255.0f;
			const float dstRf = dst[0] / 255.0f, dstGf = dst[1] / 255.0f, dstBf = dst[2] / 255.0f, dstAf = dst[3] / 255.0f;
			auto bf = [&](SwBlendFactor f, float c, bool isSrc) -> float {
				switch (f) {
					case SwBlendFactor::Zero:             return 0.0f;
					case SwBlendFactor::One:              return c;
					case SwBlendFactor::SrcAlpha:         return c * srcAf;
					case SwBlendFactor::OneMinusSrcAlpha: return c * (1.0f - srcAf);
					case SwBlendFactor::DstAlpha:         return c * dstAf;
					case SwBlendFactor::OneMinusDstAlpha: return c * (1.0f - dstAf);
					case SwBlendFactor::SrcColor:         return c * (isSrc ? 1.0f : srcRf);
					case SwBlendFactor::DstColor:         return c * (isSrc ? dstRf : 1.0f);
					default:                              return c;
				}
			};
			auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
			dst[0] = static_cast<std::uint8_t>(clamp01(bf(sFactor, srcRf, true)  + bf(dFactor, dstRf, false)) * 255.0f);
			dst[1] = static_cast<std::uint8_t>(clamp01(bf(sFactor, srcGf, true)  + bf(dFactor, dstGf, false)) * 255.0f);
			dst[2] = static_cast<std::uint8_t>(clamp01(bf(sFactor, srcBf, true)  + bf(dFactor, dstBf, false)) * 255.0f);
			dst[3] = static_cast<std::uint8_t>(clamp01(bf(sFactor, srcAf, true)  + bf(dFactor, dstAf, false)) * 255.0f);
		}

		// Wrap a signed integer texel coordinate into [0, texDim)
		inline std::int32_t WrapTexelCoord(std::int32_t idx, std::int32_t texDim, SamplerWrapping mode)
		{
			switch (mode) {
				case SamplerWrapping::Repeat:
					idx = idx % texDim;
					if (idx < 0) idx += texDim;
					return idx;
				case SamplerWrapping::MirroredRepeat: {
					std::int32_t period = texDim * 2;
					idx = idx % period;
					if (idx < 0) idx += period;
					if (idx >= texDim) idx = period - 1 - idx;
					return idx;
				}
				default: // ClampToEdge
					if (idx < 0) return 0;
					if (idx >= texDim) return texDim - 1;
					return idx;
			}
		}

		// Bilinear texture sample from 16.16 fixed-point UV coordinates.
		// uFix/vFix represent pixel coords (u * texW * 65536, v * texH * 65536).
		// Result is written to out[0..3] as RGBA bytes.
		inline void SampleBilinearFix(const std::uint8_t* texPixels, std::int32_t texW, std::int32_t texH,
		                              std::int32_t uFix, std::int32_t vFix,
		                              SamplerWrapping wrapS, SamplerWrapping wrapT,
		                              std::uint8_t* out)
		{
			// Half-pixel offset for correct bilinear centering
			const std::int32_t uf = uFix - (1 << 15);
			const std::int32_t vf = vFix - (1 << 15);

			std::int32_t x0 = uf >> 16;
			std::int32_t y0 = vf >> 16;

			// 8-bit fractional weights
			const std::int32_t fx = (uf & 0xFFFF) >> 8;
			const std::int32_t fy = (vf & 0xFFFF) >> 8;

			// Wrap all four sample coordinates
			std::int32_t x1 = WrapTexelCoord(x0 + 1, texW, wrapS);
			std::int32_t y1 = WrapTexelCoord(y0 + 1, texH, wrapT);
			x0 = WrapTexelCoord(x0, texW, wrapS);
			y0 = WrapTexelCoord(y0, texH, wrapT);

			const std::uint8_t* c00 = texPixels + (static_cast<std::size_t>(y0) * texW + x0) * 4;
			const std::uint8_t* c10 = texPixels + (static_cast<std::size_t>(y0) * texW + x1) * 4;
			const std::uint8_t* c01 = texPixels + (static_cast<std::size_t>(y1) * texW + x0) * 4;
			const std::uint8_t* c11 = texPixels + (static_cast<std::size_t>(y1) * texW + x1) * 4;

			const std::int32_t ifx = 255 - fx;
			const std::int32_t ify = 255 - fy;
			const std::int32_t w00 = ifx * ify;
			const std::int32_t w10 = fx * ify;
			const std::int32_t w01 = ifx * fy;
			const std::int32_t w11 = fx * fy;

			out[0] = static_cast<std::uint8_t>((c00[0] * w00 + c10[0] * w10 + c01[0] * w01 + c11[0] * w11) >> 16);
			out[1] = static_cast<std::uint8_t>((c00[1] * w00 + c10[1] * w10 + c01[1] * w01 + c11[1] * w11) >> 16);
			out[2] = static_cast<std::uint8_t>((c00[2] * w00 + c10[2] * w10 + c01[2] * w01 + c11[2] * w11) >> 16);
			out[3] = static_cast<std::uint8_t>((c00[3] * w00 + c10[3] * w10 + c01[3] * w01 + c11[3] * w11) >> 16);
		}

		// Pre-computed bilinear Y context for scanline-optimized sampling.
		// Y coordinates and weights are constant across a scanline, so precompute once.
		struct BilinearRowCtx
		{
			const std::uint8_t* row0;
			const std::uint8_t* row1;
			std::int32_t ify; // 255 - fy (top row weight)
			std::int32_t fy;  // fy (bottom row weight)
		};

		inline BilinearRowCtx PrepareBilinearRow(const std::uint8_t* texPixels, std::int32_t texW, std::int32_t texH,
		                                         std::int32_t vFix, SamplerWrapping wrapT)
		{
			const std::int32_t vf = vFix - (1 << 15);
			std::int32_t y0 = vf >> 16;
			const std::int32_t fy = (vf & 0xFFFF) >> 8;
			std::int32_t y1 = WrapTexelCoord(y0 + 1, texH, wrapT);
			y0 = WrapTexelCoord(y0, texH, wrapT);
			return {
				texPixels + static_cast<std::size_t>(y0) * texW * 4,
				texPixels + static_cast<std::size_t>(y1) * texW * 4,
				255 - fy, fy
			};
		}

		inline void SampleBilinearFixRow(const BilinearRowCtx& row, std::int32_t texW,
		                                 std::int32_t uFix, SamplerWrapping wrapS,
		                                 std::uint8_t* out)
		{
			const std::int32_t uf = uFix - (1 << 15);
			std::int32_t x0 = uf >> 16;
			const std::int32_t fx = (uf & 0xFFFF) >> 8;
			std::int32_t x1 = WrapTexelCoord(x0 + 1, texW, wrapS);
			x0 = WrapTexelCoord(x0, texW, wrapS);

			const std::uint8_t* c00 = row.row0 + x0 * 4;
			const std::uint8_t* c10 = row.row0 + x1 * 4;
			const std::uint8_t* c01 = row.row1 + x0 * 4;
			const std::uint8_t* c11 = row.row1 + x1 * 4;

			const std::int32_t ifx = 255 - fx;
			const std::int32_t w00 = ifx * row.ify;
			const std::int32_t w10 = fx * row.ify;
			const std::int32_t w01 = ifx * row.fy;
			const std::int32_t w11 = fx * row.fy;

			out[0] = static_cast<std::uint8_t>((c00[0] * w00 + c10[0] * w10 + c01[0] * w01 + c11[0] * w11) >> 16);
			out[1] = static_cast<std::uint8_t>((c00[1] * w00 + c10[1] * w10 + c01[1] * w01 + c11[1] * w11) >> 16);
			out[2] = static_cast<std::uint8_t>((c00[2] * w00 + c10[2] * w10 + c01[2] * w01 + c11[2] * w11) >> 16);
			out[3] = static_cast<std::uint8_t>((c00[3] * w00 + c10[3] * w10 + c01[3] * w01 + c11[3] * w11) >> 16);
		}

		// Bilinear texture sample from float UV coordinates in [0..1] range.
		// Used by the triangle rasterizer.
		inline void SampleBilinearFloat(const std::uint8_t* texPixels, std::int32_t texW, std::int32_t texH,
		                                float u, float v,
		                                SamplerWrapping wrapS, SamplerWrapping wrapT,
		                                std::uint8_t* out)
		{
			const float uf = u * texW - 0.5f;
			const float vf = v * texH - 0.5f;

			std::int32_t x0 = static_cast<std::int32_t>(std::floor(uf));
			std::int32_t y0 = static_cast<std::int32_t>(std::floor(vf));

			std::int32_t fx = static_cast<std::int32_t>((uf - x0) * 255.0f + 0.5f);
			std::int32_t fy = static_cast<std::int32_t>((vf - y0) * 255.0f + 0.5f);
			if (fx < 0) fx = 0; if (fx > 255) fx = 255;
			if (fy < 0) fy = 0; if (fy > 255) fy = 255;

			std::int32_t x1 = WrapTexelCoord(x0 + 1, texW, wrapS);
			std::int32_t y1 = WrapTexelCoord(y0 + 1, texH, wrapT);
			x0 = WrapTexelCoord(x0, texW, wrapS);
			y0 = WrapTexelCoord(y0, texH, wrapT);

			const std::uint8_t* c00 = texPixels + (static_cast<std::size_t>(y0) * texW + x0) * 4;
			const std::uint8_t* c10 = texPixels + (static_cast<std::size_t>(y0) * texW + x1) * 4;
			const std::uint8_t* c01 = texPixels + (static_cast<std::size_t>(y1) * texW + x0) * 4;
			const std::uint8_t* c11 = texPixels + (static_cast<std::size_t>(y1) * texW + x1) * 4;

			const std::int32_t ifx = 255 - fx;
			const std::int32_t ify = 255 - fy;
			const std::int32_t w00 = ifx * ify;
			const std::int32_t w10 = fx * ify;
			const std::int32_t w01 = ifx * fy;
			const std::int32_t w11 = fx * fy;

			out[0] = static_cast<std::uint8_t>((c00[0] * w00 + c10[0] * w10 + c01[0] * w01 + c11[0] * w11) >> 16);
			out[1] = static_cast<std::uint8_t>((c00[1] * w00 + c10[1] * w10 + c01[1] * w01 + c11[1] * w11) >> 16);
			out[2] = static_cast<std::uint8_t>((c00[2] * w00 + c10[2] * w10 + c01[2] * w01 + c11[2] * w11) >> 16);
			out[3] = static_cast<std::uint8_t>((c00[3] * w00 + c10[3] * w10 + c01[3] * w01 + c11[3] * w11) >> 16);
		}

		// =====================================================================
		// Fast blit: direct memcpy / stretch for simple fullscreen texture copies.
		// Returns true if the draw was handled as a fast blit.
		// =====================================================================
		bool TryFastBlit(const DrawContext& ctx, PrimitiveType type, std::int32_t firstVertex, std::int32_t count)
		{
			// Only handles procedural fullscreen quads
			if (type != PrimitiveType::TriangleStrip || count != 4 || firstVertex != 0) return false;
			if (ctx.vertexData != nullptr) return false;
			if (ctx.fragmentShader != nullptr) return false;
			if (ctx.scissorEnabled) return false;

			// Must be textured with white tint (pure blit)
			if (!ctx.ff.hasTexture) return false;
			if (ctx.ff.color[0] < 0.999f || ctx.ff.color[1] < 0.999f ||
			    ctx.ff.color[2] < 0.999f || ctx.ff.color[3] < 0.999f) return false;

			// Must cover full texRect
			if (ctx.ff.texRect[0] < 0.999f || ctx.ff.texRect[2] < 0.999f) return false;

			// No blending (opaque copy)
			if (ctx.blendingEnabled) return false;

			const SwTexture* tex = ctx.textures[ctx.ff.textureUnit];
			if (tex == nullptr) return false;
			const std::uint8_t* srcPixels = tex->GetPixels(0);
			if (srcPixels == nullptr) return false;

			const std::int32_t srcW = tex->GetWidth();
			const std::int32_t srcH = tex->GetHeight();
			if (srcW <= 0 || srcH <= 0) return false;

			// Check that spriteSize covers the full viewport (± 1 pixel tolerance)
			const std::int32_t vpW = g_state.viewportW;
			const std::int32_t vpH = g_state.viewportH;
			if (std::abs(static_cast<std::int32_t>(ctx.ff.spriteSize[0]) - vpW) > 1) return false;
			if (std::abs(static_cast<std::int32_t>(ctx.ff.spriteSize[1]) - vpH) > 1) return false;

			// Verify the MVP maps (0,0)-(spriteSize) to cover the viewport
			// For a standard fullscreen sprite, m[0]*spriteSize[0] should map to +1 or -1 in NDC
			const float* m = ctx.ff.mvpMatrix;
			// After viewport transform: x = (ndcX + 1) * 0.5 * vpW + vpX
			// For full coverage: NDC x ranges from -1 to +1 → screen 0 to vpW
			// That means m[0]*spriteSize[0] + m[12] should be ~+1 (right edge)
			// and m[12] should be ~-1 (left edge in NDC)
			float ndcLeft = m[12];
			float ndcRight = m[0] * ctx.ff.spriteSize[0] + m[12];
			float ndcTop = m[5] * ctx.ff.spriteSize[1] + m[13];
			float ndcBottom = m[13];
			if (std::fabs(ndcLeft - (-1.0f)) > 0.01f || std::fabs(ndcRight - 1.0f) > 0.01f) return false;
			// Allow both normal (bottom=-1, top=+1) and Y-flipped (bottom=+1, top=-1) quads
			if (std::fabs(ndcBottom - (-1.0f)) < 0.01f && std::fabs(ndcTop - 1.0f) < 0.01f) {
				// Normal orientation
			} else if (std::fabs(ndcBottom - 1.0f) < 0.01f && std::fabs(ndcTop - (-1.0f)) < 0.01f) {
				// Y-flipped quad (common for top-down game coordinate projections)
			} else {
				return false;
			}
			// No rotation
			if (std::fabs(m[1]) > 0.001f || std::fabs(m[4]) > 0.001f) return false;

			std::uint8_t* dstBuffer = g_state.colorBuffer;
			const std::int32_t dstW = g_state.bufferWidth;
			const std::int32_t dstH = g_state.bufferHeight;
			if (dstBuffer == nullptr) return false;

			// Flush any pending deferred draws first, so this opaque full-screen blit (which bypasses the
			// tile queue and writes the buffer directly) lands after everything submitted before it.
			SwTileRenderer::Flush();

			// Y-flip needed when source and destination have different row orders:
			// - FBO textures/buffers are stored bottom-up (row 0 = bottom)
			// - Regular textures and screen buffer are stored top-down (row 0 = top)
			// The quad's Y orientation in NDC is irrelevant — it's just the coordinate system,
			// not an intentional "flip the image" instruction.
			const bool sourceIsFbo = tex->IsRenderTarget();
			const bool flipY = (sourceIsFbo != g_state.isFboTarget);

			if (srcW == dstW && srcH == dstH) {
				// Identity blit: direct memcpy (fastest path)
				if (!flipY) {
					std::memcpy(dstBuffer, srcPixels, static_cast<std::size_t>(srcW) * srcH * 4);
				} else {
					// FBO target: flip Y during copy
					const std::int32_t rowBytes = srcW * 4;
					for (std::int32_t y = 0; y < srcH; y++) {
						const std::uint8_t* srcRow = srcPixels + y * rowBytes;
						std::uint8_t* dstRow = dstBuffer + (dstH - 1 - y) * rowBytes;
						std::memcpy(dstRow, srcRow, rowBytes);
					}
				}
			} else {
				// Nearest-neighbor stretch blit
				const std::int32_t dstRowBytes = dstW * 4;
				// Fixed-point scale factors (16.16)
				const std::uint32_t scaleX_fp = (static_cast<std::uint32_t>(srcW) << 16) / static_cast<std::uint32_t>(dstW);
				const std::uint32_t scaleY_fp = (static_cast<std::uint32_t>(srcH) << 16) / static_cast<std::uint32_t>(dstH);

				for (std::int32_t dy = 0; dy < dstH; dy++) {
					const std::int32_t sy = static_cast<std::int32_t>((static_cast<std::uint32_t>(dy) * scaleY_fp) >> 16);
					const std::int32_t actualDstY = flipY ? (dstH - 1 - dy) : dy;
					std::uint8_t* dstRow = dstBuffer + actualDstY * dstRowBytes;
					const std::uint8_t* srcRow = srcPixels + sy * srcW * 4;

					if (srcW == dstW) {
						// Same width, different height: row copy
						std::memcpy(dstRow, srcRow, dstRowBytes);
					} else {
						// Different width: nearest-neighbor horizontal stretch
						std::uint32_t srcX_fp = 0;
						const std::uint32_t* srcRow32 = reinterpret_cast<const std::uint32_t*>(srcRow);
						std::uint32_t* dstRow32 = reinterpret_cast<std::uint32_t*>(dstRow);
						for (std::int32_t dx = 0; dx < dstW; dx++) {
							const std::int32_t sx = static_cast<std::int32_t>(srcX_fp >> 16);
							dstRow32[dx] = srcRow32[sx];
							srcX_fp += scaleX_fp;
						}
					}
				}
			}

			return true;
		}

		// =====================================================================
		// Vertex fetch (screen-space, after MVP + viewport transform)
		// =====================================================================
		Vertex2D FetchVertex(const DrawContext& ctx, std::int32_t index)
		{
			Vertex2D out = { 0, 0, 0, 0, 1, 1, 1, 1 };

			if DEATH_LIKELY(ctx.vertexData == nullptr) {
				// Procedural sprite quad — replicate the sprite_vs.glsl vertex synthesis
				const float ax = ((index & ~1) == 0) ? 1.0f : 0.0f;
				const float ay = (index & 1) ? 1.0f : 0.0f;

				const float wx = ax * ctx.ff.spriteSize[0];
				const float wy = ay * ctx.ff.spriteSize[1];

				const float* m = ctx.ff.mvpMatrix;
				out.x = m[0] * wx + m[4] * wy + m[12];
				out.y = m[1] * wx + m[5] * wy + m[13];

				out.u = ax * ctx.ff.texRect[0] + ctx.ff.texRect[1];
				out.v = ay * ctx.ff.texRect[2] + ctx.ff.texRect[3];

				out.r = ctx.ff.color[0];
				out.g = ctx.ff.color[1];
				out.b = ctx.ff.color[2];
				out.a = ctx.ff.color[3];
			} else {
				// General path: interleaved clip-space vertices [x, y, u, v]. The device pre-transforms the
				// positions (no MVP is applied here), matching the source's attribute-fed general path.
				std::int32_t stride = ctx.vertexStride;
				if DEATH_UNLIKELY(stride <= 0) stride = 4 * static_cast<std::int32_t>(sizeof(float));
				const std::uint8_t* base = static_cast<const std::uint8_t*>(ctx.vertexData);
				const float* f = reinterpret_cast<const float*>(base + static_cast<std::size_t>(index) * stride);
				out.x = f[0];
				out.y = f[1];
				out.u = f[2];
				out.v = f[3];

				out.r = ctx.ff.color[0];
				out.g = ctx.ff.color[1];
				out.b = ctx.ff.color[2];
				out.a = ctx.ff.color[3];
			}

			// Viewport transform: NDC [-1,+1] → screen pixel coordinates
			out.x = (out.x + 1.0f) * 0.5f * static_cast<float>(g_state.viewportW) + static_cast<float>(g_state.viewportX);
			out.y = (1.0f - out.y) * 0.5f * static_cast<float>(g_state.viewportH) + static_cast<float>(g_state.viewportY);

			// Snap to pixel grid when within epsilon of an integer.
			// Eliminates 1-scanline flicker caused by float precision drift in MVP calculation.
			constexpr float SnapEps = 1.0f / 128.0f;
			float rxSnap = std::round(out.x);
			float rySnap = std::round(out.y);
			if (std::fabs(out.x - rxSnap) < SnapEps) out.x = rxSnap;
			if (std::fabs(out.y - rySnap) < SnapEps) out.y = rySnap;

			return out;
		}

		// =====================================================================
		// Axis-aligned quad rasterizer (fast path for non-rotated sprites)
		// Supports all SamplerWrapping modes and SIMD blending.
		// =====================================================================
		void DrawAxisAlignedQuad(const DrawContext& ctx, Vertex2D v0, Vertex2D v1, Vertex2D v2, Vertex2D v3)
		{
			if DEATH_UNLIKELY(g_state.colorBuffer == nullptr) return;

			// Screen-space bounding box from all four vertices
			const float fxMin = std::min({v0.x, v1.x, v2.x, v3.x});
			const float fxMax = std::max({v0.x, v1.x, v2.x, v3.x});
			const float fyMin = std::min({v0.y, v1.y, v2.y, v3.y});
			const float fyMax = std::max({v0.y, v1.y, v2.y, v3.y});

			const float fullW = fxMax - fxMin;
			const float fullH = fyMax - fyMin;
			if DEATH_UNLIKELY(fullW < 0.5f || fullH < 0.5f) {
				return;
			}

			// UV corners
			const float uLeft  = (v2.x <= v0.x) ? v2.u : v0.u;
			const float uRight = (v2.x <= v0.x) ? v0.u : v2.u;
			const float vTop   = (v0.y <= v1.y) ? v0.v : v1.v;
			const float vBot   = (v0.y <= v1.y) ? v1.v : v0.v;

			// Pixel bbox clamped to buffer
			std::int32_t xMin = std::max(0, static_cast<std::int32_t>(fxMin));
			std::int32_t xMax = std::min(g_state.bufferWidth  - 1, static_cast<std::int32_t>(fxMax - 0.5f));
			std::int32_t yMin = std::max(0, static_cast<std::int32_t>(fyMin));
			std::int32_t yMax = std::min(g_state.bufferHeight - 1, static_cast<std::int32_t>(fyMax - 0.5f));

			// Scissor pre-clip (Y always flipped — scissor is in bottom-up window coordinates)
			if DEATH_UNLIKELY(g_state.scissorEnabled) {
				xMin = std::max(xMin, g_state.scissorX);
				xMax = std::min(xMax, g_state.scissorX + g_state.scissorW - 1);
				const std::int32_t pyMin = g_state.bufferHeight - g_state.scissorY - g_state.scissorH;
				const std::int32_t pyMax = g_state.bufferHeight - 1 - g_state.scissorY;
				yMin = std::max(yMin, pyMin);
				yMax = std::min(yMax, pyMax);
			}
			if DEATH_UNLIKELY(xMin > xMax || yMin > yMax) {
				return;
			}

			// Texture info
			const SwTexture* tex = (ctx.ff.hasTexture && ctx.ff.textureUnit < static_cast<std::int32_t>(MaxTextureUnits)
							? ctx.textures[ctx.ff.textureUnit] : nullptr);
			const std::uint8_t* texPixels = (tex != nullptr ? tex->GetPixels(0) : nullptr);
			const std::int32_t texW = (tex != nullptr ? tex->GetWidth()  : 0);
			const std::int32_t texH = (tex != nullptr ? tex->GetHeight() : 0);
			const SamplerWrapping wrapS = (tex != nullptr ? tex->GetWrapS() : SamplerWrapping::ClampToEdge);
			const SamplerWrapping wrapT = (tex != nullptr ? tex->GetWrapT() : SamplerWrapping::ClampToEdge);
			const bool useLinear = (tex != nullptr && tex->GetMagFilter() == SamplerFilter::Linear && texW > 1 && texH > 1);

			// Tint color as [0..255]
			const std::int32_t tR = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[0]) * 255.0f + 0.5f);
			const std::int32_t tG = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[1]) * 255.0f + 0.5f);
			const std::int32_t tB = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[2]) * 255.0f + 0.5f);
			const std::int32_t tA = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[3]) * 255.0f + 0.5f);
			const bool whiteTint = (tR >= 255 && tG >= 255 && tB >= 255 && tA >= 255);

			const bool useBlend = g_state.blendingEnabled;
			const bool useFastBlend = (useBlend &&
				g_state.blendSrc == SwBlendFactor::SrcAlpha &&
				g_state.blendDst == SwBlendFactor::OneMinusSrcAlpha);

			// 16.16 fixed-point UV steps
			const std::int32_t texWFix = texW << 16;
			const std::int32_t texHFix = texH << 16;
			const std::int32_t dtxFix = (texPixels != nullptr ? static_cast<std::int32_t>((uRight - uLeft) * texW * 65536.0f / fullW) : 0);
			const std::int32_t dtyFix = (texPixels != nullptr ? static_cast<std::int32_t>((vBot - vTop) * texH * 65536.0f / fullH) : 0);

			const std::int32_t txBase = (texPixels != nullptr ? static_cast<std::int32_t>((uLeft + (xMin + 0.5f - fxMin) * (uRight - uLeft) / fullW) * texW * 65536.0f) : 0);
			std::int32_t tyFix = (texPixels != nullptr ? static_cast<std::int32_t>((vTop + (yMin + 0.5f - fyMin) * (vBot - vTop) / fullH) * texH * 65536.0f) : 0);

			const bool useRepeatS = (wrapS == SamplerWrapping::Repeat);
			const bool useRepeatT = (wrapT == SamplerWrapping::Repeat);
			const bool useClampS = (wrapS == SamplerWrapping::ClampToEdge);
			// Check if all UVs are safely within texture bounds (skip per-pixel wrapping)
			const bool uvSafeX = (useClampS && txBase >= 0 && (txBase + dtxFix * (xMax - xMin)) >= 0 &&
									(txBase >> 16) < texW && ((txBase + dtxFix * (xMax - xMin)) >> 16) < texW);

			const std::int32_t scanWidth = xMax - xMin + 1;

			// Stack-allocated scanline buffer for SIMD blending or direct copy
			constexpr std::int32_t MaxScanBuf = 4096;
			alignas(32) std::uint8_t scanBuf[MaxScanBuf * 4];
			const bool useScanBuf = ((useFastBlend || !useBlend) && scanWidth <= MaxScanBuf && texPixels != nullptr);

			for (std::int32_t py = yMin; py <= yMax; py++, tyFix += dtyFix) {
				// Y is flipped when the destination is a render-target texture (stored bottom-up)
				const std::int32_t storeY = (g_state.isFboTarget ? (g_state.bufferHeight - 1 - py) : py);
				std::uint8_t* dstRow = (g_state.colorBuffer + (storeY * g_state.bufferWidth + xMin) * 4);

				// Get source Y with wrapping
				std::int32_t srcY;
				if DEATH_LIKELY(texPixels != nullptr) {
					if (useRepeatT) {
						srcY = WrapTexelFix(tyFix, texH, SamplerWrapping::Repeat);
					} else {
						srcY = WrapTexelFix(tyFix, texH, wrapT);
					}
				} else {
					srcY = 0;
				}
				const std::uint8_t* texRow = (texPixels != nullptr ? texPixels + static_cast<std::size_t>(srcY) * texW * 4 : nullptr);

				if DEATH_LIKELY(useScanBuf) {
					// === Scanline buffer path: gather raw texels → callback/tint → SIMD blend ===
					std::int32_t txFix = txBase;

					if (useRepeatS && texPixels != nullptr && !useLinear) {
						txFix = NormalizeRepeatFix(txFix, texWFix);
					}

					// Phase 1: gather RAW texels into scanBuf (no tint applied)
					if (useLinear && texPixels != nullptr) {
						const BilinearRowCtx brow = PrepareBilinearRow(texPixels, texW, texH, tyFix, wrapT);
						for (std::int32_t i = 0; i < scanWidth; ++i) {
							SampleBilinearFixRow(brow, texW, txFix, wrapS, &scanBuf[i * 4]);
							txFix += dtxFix;
						}
					} else if DEATH_LIKELY(texRow != nullptr) {
						if (uvSafeX && dtxFix == 65536) {
							// 1:1 texel mapping - direct memcpy
							const std::int32_t srcX = txFix >> 16;
							std::memcpy(scanBuf, &texRow[srcX * 4], static_cast<std::size_t>(scanWidth) * 4);
							txFix += dtxFix * scanWidth;
						} else if (uvSafeX) {
							for (std::int32_t i = 0; i < scanWidth; i++) {
								const std::int32_t srcX = txFix >> 16;
								std::memcpy(&scanBuf[i * 4], &texRow[srcX * 4], 4);
								txFix += dtxFix;
							}
						} else if (useRepeatS) {
							for (std::int32_t i = 0; i < scanWidth; i++) {
								const std::int32_t srcX = txFix >> 16;
								std::memcpy(&scanBuf[i * 4], &texRow[srcX * 4], 4);
								txFix = AdvanceRepeatFix(txFix, dtxFix, texWFix);
							}
						} else {
							for (std::int32_t i = 0; i < scanWidth; i++) {
								const std::int32_t srcX = WrapTexelFix(txFix, texW, wrapS);
								std::memcpy(&scanBuf[i * 4], &texRow[srcX * 4], 4);
								txFix += dtxFix;
							}
						}
					}

					// Phase 2: apply fragment shader callback or vertex-color tint
					if DEATH_UNLIKELY(ctx.fragmentShader != nullptr) {
						FragmentShaderInput fsInput;
						fsInput.v = tyFix / 65536.0f / static_cast<float>(texH > 0 ? texH : 1);
						fsInput.texWidth = texW;
						fsInput.texHeight = texH;
						fsInput.textures = ctx.textures;
						fsInput.color = ctx.ff.color;
						fsInput.userData = ctx.fragmentShaderUserData;
						const float invTexW = 1.0f / static_cast<float>(texW > 0 ? texW : 1);
						std::int32_t txFixShader = txBase;
						for (std::int32_t i = 0; i < scanWidth; i++) {
							fsInput.rgba = &scanBuf[i * 4];
							fsInput.u = txFixShader / 65536.0f * invTexW;
							fsInput.x = xMin + i;
							fsInput.y = py;
							ctx.fragmentShader(fsInput);
							txFixShader += dtxFix;
						}
					} else if DEATH_UNLIKELY(!whiteTint) {
						tintScanline(scanBuf, scanWidth, tR, tG, tB, tA);
					}

					// Phase 3: blend or direct copy to framebuffer
					if (useFastBlend) {
						blendScanlineSrcAlpha(dstRow, scanBuf, scanWidth);
					} else {
						// No blending - direct copy
						std::memcpy(dstRow, scanBuf, static_cast<std::size_t>(scanWidth) * 4);
					}
				} else {
					// === Direct per-pixel path (no SIMD, supports all blend modes) ===
					std::int32_t txFix = txBase;
					if (useRepeatS && texPixels != nullptr && !useLinear) {
						txFix = NormalizeRepeatFix(txFix, texWFix);
					}

					for (std::int32_t px = xMin; px <= xMax; px++, dstRow += 4) {
						std::int32_t sR, sG, sB, sA;
						if (useLinear && texPixels != nullptr) {
							std::uint8_t raw[4];
							SampleBilinearFix(texPixels, texW, texH, txFix, tyFix, wrapS, wrapT, raw);
							txFix += dtxFix;
							sR = raw[0]; sG = raw[1]; sB = raw[2]; sA = raw[3];
						} else if (texRow != nullptr) {
							std::int32_t srcX;
							if (useRepeatS) {
								srcX = txFix >> 16;
								txFix = AdvanceRepeatFix(txFix, dtxFix, texWFix);
							} else if (uvSafeX) {
								srcX = txFix >> 16;
								txFix += dtxFix;
							} else {
								srcX = WrapTexelFix(txFix, texW, wrapS);
								txFix += dtxFix;
							}
							const std::uint8_t* src = &texRow[srcX * 4];
							sR = src[0]; sG = src[1]; sB = src[2]; sA = src[3];
						} else {
							sR = 255; sG = 255; sB = 255; sA = 255;
							txFix += dtxFix;
						}

						// Apply fragment shader callback or vertex-color tint
						if DEATH_UNLIKELY(ctx.fragmentShader != nullptr) {
							std::uint8_t px4[4] = {
								static_cast<std::uint8_t>(sR), static_cast<std::uint8_t>(sG),
								static_cast<std::uint8_t>(sB), static_cast<std::uint8_t>(sA)
							};
							FragmentShaderInput fsInput;
							fsInput.rgba = px4;
							fsInput.u = (txFix - dtxFix) / 65536.0f / static_cast<float>(texW > 0 ? texW : 1);
							fsInput.v = tyFix / 65536.0f / static_cast<float>(texH > 0 ? texH : 1);
							fsInput.x = px;
							fsInput.y = py;
							fsInput.texWidth = texW;
							fsInput.texHeight = texH;
							fsInput.textures = ctx.textures;
							fsInput.color = ctx.ff.color;
							fsInput.userData = ctx.fragmentShaderUserData;
							ctx.fragmentShader(fsInput);
							sR = px4[0]; sG = px4[1]; sB = px4[2]; sA = px4[3];
						} else if DEATH_UNLIKELY(!whiteTint) {
							sR = (sR * tR) >> 8;
							sG = (sG * tG) >> 8;
							sB = (sB * tB) >> 8;
							sA = (sA * tA) >> 8;
						}

						if (useBlend) {
							if (sA == 0) {
								continue;
							}

							if (useFastBlend) {
								if (sA >= 255) {
									dstRow[0] = static_cast<std::uint8_t>(sR);
									dstRow[1] = static_cast<std::uint8_t>(sG);
									dstRow[2] = static_cast<std::uint8_t>(sB);
									dstRow[3] = static_cast<std::uint8_t>(sA);
								} else {
									const std::int32_t inv = 255 - sA;
									dstRow[0] = static_cast<std::uint8_t>((sR * sA + dstRow[0] * inv) >> 8);
									dstRow[1] = static_cast<std::uint8_t>((sG * sA + dstRow[1] * inv) >> 8);
									dstRow[2] = static_cast<std::uint8_t>((sB * sA + dstRow[2] * inv) >> 8);
									dstRow[3] = static_cast<std::uint8_t>((sA * 255 + dstRow[3] * inv) >> 8);
								}
							} else {
								BlendPixelGeneric(dstRow, sR, sG, sB, sA, g_state.blendSrc, g_state.blendDst);
							}
						} else {
							dstRow[0] = static_cast<std::uint8_t>(sR);
							dstRow[1] = static_cast<std::uint8_t>(sG);
							dstRow[2] = static_cast<std::uint8_t>(sB);
							dstRow[3] = static_cast<std::uint8_t>(sA);
						}
					}
				}
			}
		}

		// =====================================================================
		// Triangle rasterizer - incremental edge functions, pre-clipped
		// Supports all SamplerWrapping modes and blend modes.
		// =====================================================================
		void RasterizeTriangle(const DrawContext& ctx, Vertex2D v0, Vertex2D v1, Vertex2D v2)
		{
			// Bounding box in screen space
			std::int32_t minX = std::max(0, static_cast<std::int32_t>(std::min({v0.x, v1.x, v2.x})));
			std::int32_t maxX = std::min(g_state.bufferWidth  - 1, static_cast<std::int32_t>(std::max({v0.x, v1.x, v2.x})));
			std::int32_t minY = std::max(0, static_cast<std::int32_t>(std::min({v0.y, v1.y, v2.y})));
			std::int32_t maxY = std::min(g_state.bufferHeight - 1, static_cast<std::int32_t>(std::max({v0.y, v1.y, v2.y})));

			// Pre-clip to scissor (Y always flipped — scissor is in bottom-up window coordinates)
			if DEATH_UNLIKELY(g_state.scissorEnabled) {
				const std::int32_t pyMin = g_state.bufferHeight - g_state.scissorY - g_state.scissorH;
				const std::int32_t pyMax = g_state.bufferHeight - 1 - g_state.scissorY;
				minX = std::max(minX, g_state.scissorX);
				maxX = std::min(maxX, g_state.scissorX + g_state.scissorW - 1);
				minY = std::max(minY, pyMin);
				maxY = std::min(maxY, pyMax);
			}
			if DEATH_UNLIKELY(minX > maxX || minY > maxY) {
				return;
			}

			// Edge function: E(A→B, P) = (B.x-A.x)*(P.y-A.y) - (B.y-A.y)*(P.x-A.x)
			// Incremental: dE/dx = -(B.y-A.y),  dE/dy = (B.x-A.x)
			float w0_dx = v1.y - v2.y, w0_dy = v2.x - v1.x;
			float w1_dx = v2.y - v0.y, w1_dy = v0.x - v2.x;
			float w2_dx = v0.y - v1.y, w2_dy = v1.x - v0.x;

			float area = (v2.x - v1.x) * (v0.y - v1.y) - (v2.y - v1.y) * (v0.x - v1.x);
			if (std::fabs(area) < 1e-6f) return; // degenerate
			// Orientation-normalize so the interior is POSITIVE for every winding, then decide the exact
			// w == 0 edge pixels with a top-left fill rule (include when the normalized edge gradient
			// points right, or straight down for horizontal edges). Two strip triangles sharing a
			// diagonal then cover each of its pixels EXACTLY once (kept identical in the tile
			// rasterizer's rasterTri, see SwTileRasterizer.cpp).
			const float orient = (area > 0.0f ? 1.0f : -1.0f);
			area *= orient;
			w0_dx *= orient; w0_dy *= orient;
			w1_dx *= orient; w1_dy *= orient;
			w2_dx *= orient; w2_dy *= orient;
			const float invArea = 1.0f / area;
			const bool incl0 = (w0_dx > 0.0f || (w0_dx == 0.0f && w0_dy > 0.0f));
			const bool incl1 = (w1_dx > 0.0f || (w1_dx == 0.0f && w1_dy > 0.0f));
			const bool incl2 = (w2_dx > 0.0f || (w2_dx == 0.0f && w2_dy > 0.0f));

			// Edge functions at pixel center (minX+0.5, minY+0.5)
			const float px0 = minX + 0.5f, py0 = minY + 0.5f;
			float w0_row = orient * ((v2.x - v1.x) * (py0 - v1.y) - (v2.y - v1.y) * (px0 - v1.x));
			float w1_row = orient * ((v0.x - v2.x) * (py0 - v2.y) - (v0.y - v2.y) * (px0 - v2.x));
			float w2_row = orient * ((v1.x - v0.x) * (py0 - v0.y) - (v1.y - v0.y) * (px0 - v0.x));

			const SwTexture* tex = (ctx.ff.hasTexture && ctx.ff.textureUnit < static_cast<std::int32_t>(MaxTextureUnits)
							? ctx.textures[ctx.ff.textureUnit] : nullptr);
			const std::uint8_t* texPixels = (tex != nullptr ? tex->GetPixels(0) : nullptr);
			const std::int32_t texW = (tex != nullptr ? tex->GetWidth() : 0);
			const std::int32_t texH = (tex != nullptr ? tex->GetHeight() : 0);
			const SamplerWrapping wrapS = (tex != nullptr ? tex->GetWrapS() : SamplerWrapping::ClampToEdge);
			const SamplerWrapping wrapT = (tex != nullptr ? tex->GetWrapT() : SamplerWrapping::ClampToEdge);
			const bool useLinear = (tex != nullptr && tex->GetMagFilter() == SamplerFilter::Linear && texW > 1 && texH > 1);

			const std::int32_t tR = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[0]) * 255.0f + 0.5f);
			const std::int32_t tG = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[1]) * 255.0f + 0.5f);
			const std::int32_t tB = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[2]) * 255.0f + 0.5f);
			const std::int32_t tA = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[3]) * 255.0f + 0.5f);

			const bool useBlend = g_state.blendingEnabled;
			const bool useFastBlend = (useBlend &&
				g_state.blendSrc == SwBlendFactor::SrcAlpha &&
				g_state.blendDst == SwBlendFactor::OneMinusSrcAlpha);

			for (std::int32_t py = minY; py <= maxY; py++) {
				float w0 = w0_row, w1 = w1_row, w2 = w2_row;
				// Y is flipped when the destination is a render-target texture (stored bottom-up)
				const std::int32_t storeY = (g_state.isFboTarget ? (g_state.bufferHeight - 1 - py) : py);
				std::uint8_t* dstRow = (g_state.colorBuffer + (storeY * g_state.bufferWidth + minX) * 4);

				for (std::int32_t px = minX; px <= maxX; ++px, dstRow += 4, w0 += w0_dx, w1 += w1_dx, w2 += w2_dx) {
					if ((w0 < 0.0f || (w0 == 0.0f && !incl0)) ||
					    (w1 < 0.0f || (w1 == 0.0f && !incl1)) ||
					    (w2 < 0.0f || (w2 == 0.0f && !incl2)))
						continue;

					const float b0 = w0 * invArea;
					const float b1 = w1 * invArea;
					const float b2 = w2 * invArea;

					float u = b0 * v0.u + b1 * v1.u + b2 * v2.u;
					float vv = b0 * v0.v + b1 * v1.v + b2 * v2.v;

					std::int32_t sR, sG, sB, sA;
					if (texPixels != nullptr && useLinear) {
						u = WrapUV(u, wrapS);
						vv = WrapUV(vv, wrapT);
						std::uint8_t raw[4];
						SampleBilinearFloat(texPixels, texW, texH, u, vv, wrapS, wrapT, raw);
						sR = raw[0]; sG = raw[1]; sB = raw[2]; sA = raw[3];
					} else if (texPixels != nullptr) {
						u = WrapUV(u, wrapS);
						vv = WrapUV(vv, wrapT);
						const std::int32_t srcX = std::max(0, std::min(texW - 1, static_cast<std::int32_t>(u * (texW - 1) + 0.5f)));
						const std::int32_t srcY = std::max(0, std::min(texH - 1, static_cast<std::int32_t>(vv * (texH - 1) + 0.5f)));
						const std::uint8_t* src = texPixels + (srcY * texW + srcX) * 4;
						sR = src[0]; sG = src[1]; sB = src[2]; sA = src[3];
					} else {
						sR = 255; sG = 255; sB = 255; sA = 255;
					}

					// Apply fragment shader callback or vertex-color tint
					if DEATH_UNLIKELY(ctx.fragmentShader != nullptr) {
						std::uint8_t px4[4] = {
							static_cast<std::uint8_t>(sR), static_cast<std::uint8_t>(sG),
							static_cast<std::uint8_t>(sB), static_cast<std::uint8_t>(sA)
						};
						FragmentShaderInput fsInput;
						fsInput.rgba = px4;
						fsInput.u = u;
						fsInput.v = vv;
						fsInput.x = px;
						fsInput.y = py;
						fsInput.texWidth = texW;
						fsInput.texHeight = texH;
						fsInput.textures = ctx.textures;
						fsInput.color = ctx.ff.color;
						fsInput.userData = ctx.fragmentShaderUserData;
						ctx.fragmentShader(fsInput);
						sR = px4[0]; sG = px4[1]; sB = px4[2]; sA = px4[3];
					} else {
						sR = (sR * tR) >> 8;
						sG = (sG * tG) >> 8;
						sB = (sB * tB) >> 8;
						sA = (sA * tA) >> 8;
					}

					if (useBlend) {
						if (sA == 0) {
							continue;
						}

						if (useFastBlend) {
							if (sA >= 255) {
								dstRow[0] = static_cast<std::uint8_t>(sR);
								dstRow[1] = static_cast<std::uint8_t>(sG);
								dstRow[2] = static_cast<std::uint8_t>(sB);
								dstRow[3] = static_cast<std::uint8_t>(sA);
							} else {
								const std::int32_t inv = 255 - sA;
								dstRow[0] = static_cast<std::uint8_t>((sR * sA + dstRow[0] * inv) >> 8);
								dstRow[1] = static_cast<std::uint8_t>((sG * sA + dstRow[1] * inv) >> 8);
								dstRow[2] = static_cast<std::uint8_t>((sB * sA + dstRow[2] * inv) >> 8);
								dstRow[3] = static_cast<std::uint8_t>((sA * 255 + dstRow[3] * inv) >> 8);
							}
						} else {
							BlendPixelGeneric(dstRow, sR, sG, sB, sA, g_state.blendSrc, g_state.blendDst);
						}
					} else {
						dstRow[0] = static_cast<std::uint8_t>(sR);
						dstRow[1] = static_cast<std::uint8_t>(sG);
						dstRow[2] = static_cast<std::uint8_t>(sB);
						dstRow[3] = static_cast<std::uint8_t>(sA);
					}
				}

				w0_row += w0_dy;
				w1_row += w1_dy;
				w2_row += w2_dy;
			}
		}

		// =====================================================================
		// Affine quad rasterizer - scanline-based for rotated/scaled quads.
		// Rasterizes a convex quad defined by 4 vertices with UV interpolation
		// per-scanline, enabling SIMD blending. Much faster than splitting into
		// two triangles with per-pixel barycentric tests.
		// =====================================================================
		void DrawAffineQuad(const DrawContext& ctx, Vertex2D v0, Vertex2D v1, Vertex2D v2, Vertex2D v3)
		{
			if DEATH_UNLIKELY(g_state.colorBuffer == nullptr) return;

			// Reorder vertices as a quad: triangle strip (v0,v1,v2,v3) forms quad
			// with edges v0-v1, v1-v3, v3-v2, v2-v0 (or equivalently, the two triangles
			// share edge v1-v2, forming quad v0,v1,v3,v2 in CCW/CW order).
			// Reorder to sorted array by Y for scanline processing.
			struct QVert { float x, y, u, v; };
			QVert quad[4] = {
				{ v0.x, v0.y, v0.u, v0.v },
				{ v1.x, v1.y, v1.u, v1.v },
				{ v2.x, v2.y, v2.u, v2.v },
				{ v3.x, v3.y, v3.u, v3.v }
			};

			// Sort by Y (insertion sort on 4 elements)
			for (std::int32_t i = 1; i < 4; i++) {
				QVert tmp = quad[i];
				std::int32_t j = i - 1;
				while (j >= 0 && quad[j].y > tmp.y) {
					quad[j + 1] = quad[j];
					j--;
				}
				quad[j + 1] = tmp;
			}

			// Get bounding box
			float fxMin = std::min({quad[0].x, quad[1].x, quad[2].x, quad[3].x});
			float fxMax = std::max({quad[0].x, quad[1].x, quad[2].x, quad[3].x});
			float fyMin = quad[0].y;
			float fyMax = quad[3].y;

			std::int32_t yMin = std::max(0, static_cast<std::int32_t>(fyMin));
			std::int32_t yMax = std::min(g_state.bufferHeight - 1, static_cast<std::int32_t>(fyMax));
			std::int32_t xMinClamp = std::max(0, static_cast<std::int32_t>(fxMin));
			std::int32_t xMaxClamp = std::min(g_state.bufferWidth - 1, static_cast<std::int32_t>(fxMax));

			// Scissor clamp (Y always flipped — scissor is in bottom-up window coordinates)
			if DEATH_UNLIKELY(g_state.scissorEnabled) {
				const std::int32_t pyMin = g_state.bufferHeight - g_state.scissorY - g_state.scissorH;
				const std::int32_t pyMax = g_state.bufferHeight - 1 - g_state.scissorY;
				xMinClamp = std::max(xMinClamp, g_state.scissorX);
				xMaxClamp = std::min(xMaxClamp, g_state.scissorX + g_state.scissorW - 1);
				yMin = std::max(yMin, pyMin);
				yMax = std::min(yMax, pyMax);
			}
			if DEATH_UNLIKELY(yMin > yMax || xMinClamp > xMaxClamp) {
				return;
			}

			// Texture info
			const SwTexture* tex = (ctx.ff.hasTexture && ctx.ff.textureUnit < static_cast<std::int32_t>(MaxTextureUnits)
							? ctx.textures[ctx.ff.textureUnit] : nullptr);
			const std::uint8_t* texPixels = (tex != nullptr ? tex->GetPixels(0) : nullptr);
			const std::int32_t texW = (tex != nullptr ? tex->GetWidth() : 0);
			const std::int32_t texH = (tex != nullptr ? tex->GetHeight() : 0);
			const SamplerWrapping wrapS = (tex != nullptr ? tex->GetWrapS() : SamplerWrapping::ClampToEdge);
			const SamplerWrapping wrapT = (tex != nullptr ? tex->GetWrapT() : SamplerWrapping::ClampToEdge);
			const bool useLinear = (tex != nullptr && tex->GetMagFilter() == SamplerFilter::Linear && texW > 1 && texH > 1);

			const std::int32_t tR = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[0]) * 255.0f + 0.5f);
			const std::int32_t tG = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[1]) * 255.0f + 0.5f);
			const std::int32_t tB = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[2]) * 255.0f + 0.5f);
			const std::int32_t tA = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[3]) * 255.0f + 0.5f);
			const bool whiteTint = (tR >= 255 && tG >= 255 && tB >= 255 && tA >= 255);

			const bool useBlend = g_state.blendingEnabled;
			const bool useFastBlend = (useBlend &&
				g_state.blendSrc == SwBlendFactor::SrcAlpha &&
				g_state.blendDst == SwBlendFactor::OneMinusSrcAlpha);

			// Compute affine UV mapping from the first triangle (v0, v1, v2) in the original strip order
			float e1x = v2.x - v0.x, e1y = v2.y - v0.y;
			float e2x = v1.x - v0.x, e2y = v1.y - v0.y;
			float det = e1x * e2y - e1y * e2x;
			if DEATH_UNLIKELY(std::fabs(det) < 1e-6f) return; // Degenerate quad

			float invDet = 1.0f / det;
			float du_s = v2.u - v0.u, du_t = v1.u - v0.u;
			float dv_s = v2.v - v0.v, dv_t = v1.v - v0.v;

			// ds/dx = e2y * invDet, ds/dy = -e2x * invDet
			// dt/dx = -e1y * invDet, dt/dy = e1x * invDet
			float ds_dx = e2y * invDet, ds_dy = -e2x * invDet;
			float dt_dx = -e1y * invDet, dt_dy = e1x * invDet;

			float dudx = du_s * ds_dx + du_t * dt_dx;
			float dudy = du_s * ds_dy + du_t * dt_dy;
			float dvdx = dv_s * ds_dx + dv_t * dt_dx;
			float dvdy = dv_s * ds_dy + dv_t * dt_dy;

			// Scanline buffer for SIMD blending
			constexpr std::int32_t MaxScanBuf = 4096;
			alignas(32) std::uint8_t scanBuf[MaxScanBuf * 4];

			// Edge functions for triangle 1 (v0, v1, v2)
			const float t1_w0_dx = v1.y - v2.y, t1_w0_dy = v2.x - v1.x;
			const float t1_w1_dx = v2.y - v0.y, t1_w1_dy = v0.x - v2.x;
			const float t1_w2_dx = v0.y - v1.y, t1_w2_dy = v1.x - v0.x;
			const float area1 = (v2.x - v1.x) * (v0.y - v1.y) - (v2.y - v1.y) * (v0.x - v1.x);

			// Edge functions for triangle 2 (v2, v1, v3)
			const float t2_w0_dx = v1.y - v3.y, t2_w0_dy = v3.x - v1.x;
			const float t2_w1_dx = v3.y - v2.y, t2_w1_dy = v2.x - v3.x;
			const float t2_w2_dx = v2.y - v1.y, t2_w2_dy = v1.x - v2.x;
			const float area2 = (v3.x - v1.x) * (v2.y - v1.y) - (v3.y - v1.y) * (v2.x - v1.x);

			if DEATH_UNLIKELY(std::fabs(area1) < 1e-6f && std::fabs(area2) < 1e-6f) return;
			const bool sign1 = (area1 > 0.0f);
			const bool sign2 = (area2 > 0.0f);

			// 16.16 fixed-point UV steps for scanline
			const std::int32_t dudxFix = (texPixels != nullptr ? static_cast<std::int32_t>(dudx * texW * 65536.0f) : 0);
			const std::int32_t dvdxFix = (texPixels != nullptr ? static_cast<std::int32_t>(dvdx * texH * 65536.0f) : 0);

			for (std::int32_t py = yMin; py <= yMax; py++) {
				const float pyCtr = py + 0.5f;
				const std::int32_t storeY = (g_state.isFboTarget ? (g_state.bufferHeight - 1 - py) : py);

				// Initial edge function values at (xMinClamp+0.5, pyCtr)
				float px0 = xMinClamp + 0.5f;

				float t1_w0 = (v2.x - v1.x) * (pyCtr - v1.y) - (v2.y - v1.y) * (px0 - v1.x);
				float t1_w1 = (v0.x - v2.x) * (pyCtr - v2.y) - (v0.y - v2.y) * (px0 - v2.x);
				float t1_w2 = (v1.x - v0.x) * (pyCtr - v0.y) - (v1.y - v0.y) * (px0 - v0.x);

				float t2_w0 = (v3.x - v1.x) * (pyCtr - v1.y) - (v3.y - v1.y) * (px0 - v1.x);
				float t2_w1 = (v2.x - v3.x) * (pyCtr - v3.y) - (v2.y - v3.y) * (px0 - v3.x);
				float t2_w2 = (v1.x - v2.x) * (pyCtr - v2.y) - (v1.y - v2.y) * (px0 - v2.x);

				// Find left edge: first pixel inside either triangle
				std::int32_t xLeft = xMaxClamp + 1;
				std::int32_t xRight = xMinClamp - 1;

				float w1_0 = t1_w0, w1_1 = t1_w1, w1_2 = t1_w2;
				float w2_0 = t2_w0, w2_1 = t2_w1, w2_2 = t2_w2;

				for (std::int32_t px = xMinClamp; px <= xMaxClamp; px++) {
					bool in1 = (std::fabs(area1) >= 1e-6f) &&
						((w1_0 >= 0.0f) == sign1) && ((w1_1 >= 0.0f) == sign1) && ((w1_2 >= 0.0f) == sign1);
					bool in2 = (std::fabs(area2) >= 1e-6f) &&
						((w2_0 >= 0.0f) == sign2) && ((w2_1 >= 0.0f) == sign2) && ((w2_2 >= 0.0f) == sign2);
					if (in1 || in2) {
						xLeft = px;
						break;
					}
					w1_0 += t1_w0_dx; w1_1 += t1_w1_dx; w1_2 += t1_w2_dx;
					w2_0 += t2_w0_dx; w2_1 += t2_w1_dx; w2_2 += t2_w2_dx;
				}
				if (xLeft > xMaxClamp) continue;

				// Find right edge from the right
				float rpx0 = xMaxClamp + 0.5f;
				float rt1_w0 = (v2.x - v1.x) * (pyCtr - v1.y) - (v2.y - v1.y) * (rpx0 - v1.x);
				float rt1_w1 = (v0.x - v2.x) * (pyCtr - v2.y) - (v0.y - v2.y) * (rpx0 - v2.x);
				float rt1_w2 = (v1.x - v0.x) * (pyCtr - v0.y) - (v1.y - v0.y) * (rpx0 - v0.x);
				float rt2_w0 = (v3.x - v1.x) * (pyCtr - v1.y) - (v3.y - v1.y) * (rpx0 - v1.x);
				float rt2_w1 = (v2.x - v3.x) * (pyCtr - v3.y) - (v2.y - v3.y) * (rpx0 - v3.x);
				float rt2_w2 = (v1.x - v2.x) * (pyCtr - v2.y) - (v1.y - v2.y) * (rpx0 - v2.x);

				for (std::int32_t px = xMaxClamp; px >= xLeft; px--) {
					bool in1 = (std::fabs(area1) >= 1e-6f) &&
						((rt1_w0 >= 0.0f) == sign1) && ((rt1_w1 >= 0.0f) == sign1) && ((rt1_w2 >= 0.0f) == sign1);
					bool in2 = (std::fabs(area2) >= 1e-6f) &&
						((rt2_w0 >= 0.0f) == sign2) && ((rt2_w1 >= 0.0f) == sign2) && ((rt2_w2 >= 0.0f) == sign2);
					if (in1 || in2) {
						xRight = px;
						break;
					}
					rt1_w0 -= t1_w0_dx; rt1_w1 -= t1_w1_dx; rt1_w2 -= t1_w2_dx;
					rt2_w0 -= t2_w0_dx; rt2_w1 -= t2_w1_dx; rt2_w2 -= t2_w2_dx;
				}
				if (xRight < xLeft) continue;

				std::int32_t scanWidth = xRight - xLeft + 1;

				// Compute UV at the left edge using the affine mapping
				float leftCtrX = xLeft + 0.5f;
				float relX = leftCtrX - v0.x;
				float relY = pyCtr - v0.y;
				float uStart = v0.u + dudx * relX + dudy * relY;
				float vStart = v0.v + dvdx * relX + dvdy * relY;

				// Use scanline buffer when possible (fits + common blend mode)
				bool useScanBuf = ((useFastBlend || !useBlend) && scanWidth <= MaxScanBuf && texPixels != nullptr);

				if (useScanBuf) {
					// Fixed-point UV interpolation along scanline
					std::int32_t uFix = static_cast<std::int32_t>(uStart * texW * 65536.0f);
					std::int32_t vFix = static_cast<std::int32_t>(vStart * texH * 65536.0f);

					// Gather texels into scanline buffer
					if (useLinear) {
						for (std::int32_t i = 0; i < scanWidth; i++) {
							SampleBilinearFix(texPixels, texW, texH, uFix, vFix, wrapS, wrapT, &scanBuf[i * 4]);
							uFix += dudxFix;
							vFix += dvdxFix;
						}
					} else {
						for (std::int32_t i = 0; i < scanWidth; i++) {
							std::int32_t srcX = WrapTexelFix(uFix, texW, wrapS);
							std::int32_t srcY = WrapTexelFix(vFix, texH, wrapT);
							const std::uint8_t* src = texPixels + (static_cast<std::size_t>(srcY) * texW + srcX) * 4;
							std::memcpy(&scanBuf[i * 4], src, 4);
							uFix += dudxFix;
							vFix += dvdxFix;
						}
					}

					// Apply fragment shader or tint
					if DEATH_UNLIKELY(ctx.fragmentShader != nullptr) {
						FragmentShaderInput fsInput;
						fsInput.texWidth = texW;
						fsInput.texHeight = texH;
						fsInput.textures = ctx.textures;
						fsInput.color = ctx.ff.color;
						fsInput.userData = ctx.fragmentShaderUserData;
						std::int32_t uFixShader = static_cast<std::int32_t>(uStart * texW * 65536.0f);
						std::int32_t vFixShader = static_cast<std::int32_t>(vStart * texH * 65536.0f);
						const float invTexW = 1.0f / static_cast<float>(texW > 0 ? texW : 1);
						const float invTexH = 1.0f / static_cast<float>(texH > 0 ? texH : 1);
						for (std::int32_t i = 0; i < scanWidth; i++) {
							fsInput.rgba = &scanBuf[i * 4];
							fsInput.u = uFixShader / 65536.0f * invTexW;
							fsInput.v = vFixShader / 65536.0f * invTexH;
							fsInput.x = xLeft + i;
							fsInput.y = py;
							ctx.fragmentShader(fsInput);
							uFixShader += dudxFix;
							vFixShader += dvdxFix;
						}
					} else if DEATH_UNLIKELY(!whiteTint) {
						tintScanline(scanBuf, scanWidth, tR, tG, tB, tA);
					}

					// Blend or copy to framebuffer
					std::uint8_t* dstRow = g_state.colorBuffer + (storeY * g_state.bufferWidth + xLeft) * 4;
					if (useFastBlend) {
						blendScanlineSrcAlpha(dstRow, scanBuf, scanWidth);
					} else {
						std::memcpy(dstRow, scanBuf, static_cast<std::size_t>(scanWidth) * 4);
					}
				} else {
					// Per-pixel path (generic blend modes or no texture)
					std::uint8_t* dstRow = g_state.colorBuffer + (storeY * g_state.bufferWidth + xLeft) * 4;
					float u = uStart, vv = vStart;

					for (std::int32_t px = xLeft; px <= xRight; px++, dstRow += 4) {
						std::int32_t sR, sG, sB, sA;
						if (texPixels != nullptr && useLinear) {
							float wu = WrapUV(u, wrapS);
							float wv = WrapUV(vv, wrapT);
							std::uint8_t raw[4];
							SampleBilinearFloat(texPixels, texW, texH, wu, wv, wrapS, wrapT, raw);
							sR = raw[0]; sG = raw[1]; sB = raw[2]; sA = raw[3];
						} else if (texPixels != nullptr) {
							float wu = WrapUV(u, wrapS);
							float wv = WrapUV(vv, wrapT);
							std::int32_t srcX = std::max(0, std::min(texW - 1, static_cast<std::int32_t>(wu * (texW - 1) + 0.5f)));
							std::int32_t srcY = std::max(0, std::min(texH - 1, static_cast<std::int32_t>(wv * (texH - 1) + 0.5f)));
							const std::uint8_t* src = texPixels + (srcY * texW + srcX) * 4;
							sR = src[0]; sG = src[1]; sB = src[2]; sA = src[3];
						} else {
							sR = 255; sG = 255; sB = 255; sA = 255;
						}
						u += dudx;
						vv += dvdx;

						if DEATH_UNLIKELY(ctx.fragmentShader != nullptr) {
							std::uint8_t px4[4] = {
								static_cast<std::uint8_t>(sR), static_cast<std::uint8_t>(sG),
								static_cast<std::uint8_t>(sB), static_cast<std::uint8_t>(sA)
							};
							FragmentShaderInput fsInput;
							fsInput.rgba = px4;
							fsInput.u = u - dudx;
							fsInput.v = vv - dvdx;
							fsInput.x = px;
							fsInput.y = py;
							fsInput.texWidth = texW;
							fsInput.texHeight = texH;
							fsInput.textures = ctx.textures;
							fsInput.color = ctx.ff.color;
							fsInput.userData = ctx.fragmentShaderUserData;
							ctx.fragmentShader(fsInput);
							sR = px4[0]; sG = px4[1]; sB = px4[2]; sA = px4[3];
						} else if DEATH_UNLIKELY(!whiteTint) {
							sR = (sR * tR) >> 8;
							sG = (sG * tG) >> 8;
							sB = (sB * tB) >> 8;
							sA = (sA * tA) >> 8;
						}

						if (useBlend) {
							if (sA == 0) continue;
							if (useFastBlend) {
								if (sA >= 255) {
									dstRow[0] = static_cast<std::uint8_t>(sR);
									dstRow[1] = static_cast<std::uint8_t>(sG);
									dstRow[2] = static_cast<std::uint8_t>(sB);
									dstRow[3] = static_cast<std::uint8_t>(sA);
								} else {
									const std::int32_t inv = 255 - sA;
									dstRow[0] = static_cast<std::uint8_t>((sR * sA + dstRow[0] * inv) >> 8);
									dstRow[1] = static_cast<std::uint8_t>((sG * sA + dstRow[1] * inv) >> 8);
									dstRow[2] = static_cast<std::uint8_t>((sB * sA + dstRow[2] * inv) >> 8);
									dstRow[3] = static_cast<std::uint8_t>((sA * 255 + dstRow[3] * inv) >> 8);
								}
							} else {
								BlendPixelGeneric(dstRow, sR, sG, sB, sA, g_state.blendSrc, g_state.blendDst);
							}
						} else {
							dstRow[0] = static_cast<std::uint8_t>(sR);
							dstRow[1] = static_cast<std::uint8_t>(sG);
							dstRow[2] = static_cast<std::uint8_t>(sB);
							dstRow[3] = static_cast<std::uint8_t>(sA);
						}
					}
				}
			}
		}

		// =====================================================================
		// Line rasterizer - a clipped DDA with interpolated texture coordinates.
		// Walks one pixel per major-axis step from the start toward the end,
		// excluding the final endpoint (the GL diamond-exit convention), so the
		// shared joint of two strip segments is never blended twice. Each pixel
		// runs the same sample -> fragment/tint -> blend pipeline as
		// RasterizeTriangle. Consumers: the HUD weapon wheel draws its arcs as
		// textured LineStrip mesh commands.
		// =====================================================================
		void RasterizeLine(const DrawContext& ctx, const Vertex2D& v0, const Vertex2D& v1)
		{
			if DEATH_UNLIKELY(g_state.colorBuffer == nullptr) {
				return;
			}

			// Inclusive pixel clip rectangle (scissor Y flipped - it is in bottom-up window coordinates)
			std::int32_t clipMinX = 0, clipMaxX = g_state.bufferWidth - 1;
			std::int32_t clipMinY = 0, clipMaxY = g_state.bufferHeight - 1;
			if DEATH_UNLIKELY(g_state.scissorEnabled) {
				clipMinX = std::max(clipMinX, g_state.scissorX);
				clipMaxX = std::min(clipMaxX, g_state.scissorX + g_state.scissorW - 1);
				clipMinY = std::max(clipMinY, g_state.bufferHeight - g_state.scissorY - g_state.scissorH);
				clipMaxY = std::min(clipMaxY, g_state.bufferHeight - 1 - g_state.scissorY);
			}
			if DEATH_UNLIKELY(clipMinX > clipMaxX || clipMinY > clipMaxY) {
				return;
			}

			const SwTexture* tex = (ctx.ff.hasTexture && ctx.ff.textureUnit < static_cast<std::int32_t>(MaxTextureUnits)
							? ctx.textures[ctx.ff.textureUnit] : nullptr);
			const std::uint8_t* texPixels = (tex != nullptr ? tex->GetPixels(0) : nullptr);
			const std::int32_t texW = (tex != nullptr ? tex->GetWidth() : 0);
			const std::int32_t texH = (tex != nullptr ? tex->GetHeight() : 0);
			if DEATH_UNLIKELY(texPixels != nullptr && (texW <= 0 || texH <= 0)) {
				texPixels = nullptr;	// Malformed texture - treat as untextured
			}
			const SamplerWrapping wrapS = (tex != nullptr ? tex->GetWrapS() : SamplerWrapping::ClampToEdge);
			const SamplerWrapping wrapT = (tex != nullptr ? tex->GetWrapT() : SamplerWrapping::ClampToEdge);
			const bool useLinear = (tex != nullptr && tex->GetMagFilter() == SamplerFilter::Linear && texW > 1 && texH > 1);

			const std::int32_t tR = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[0]) * 255.0f + 0.5f);
			const std::int32_t tG = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[1]) * 255.0f + 0.5f);
			const std::int32_t tB = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[2]) * 255.0f + 0.5f);
			const std::int32_t tA = static_cast<std::int32_t>(std::min(1.0f, ctx.ff.color[3]) * 255.0f + 0.5f);

			const bool useBlend = g_state.blendingEnabled;
			const bool useFastBlend = (useBlend &&
				g_state.blendSrc == SwBlendFactor::SrcAlpha &&
				g_state.blendDst == SwBlendFactor::OneMinusSrcAlpha);

			const float dx = v1.x - v0.x;
			const float dy = v1.y - v0.y;
			const std::int32_t steps = std::max(1, static_cast<std::int32_t>(std::ceil(std::max(std::fabs(dx), std::fabs(dy)))));
			const float invSteps = 1.0f / static_cast<float>(steps);

			for (std::int32_t i = 0; i < steps; i++) {
				const float t = static_cast<float>(i) * invSteps;
				const std::int32_t px = static_cast<std::int32_t>(v0.x + dx * t);
				const std::int32_t py = static_cast<std::int32_t>(v0.y + dy * t);
				if (px < clipMinX || px > clipMaxX || py < clipMinY || py > clipMaxY) {
					continue;
				}

				float u = v0.u + (v1.u - v0.u) * t;
				float vv = v0.v + (v1.v - v0.v) * t;

				std::int32_t sR, sG, sB, sA;
				if (texPixels != nullptr && useLinear) {
					u = WrapUV(u, wrapS);
					vv = WrapUV(vv, wrapT);
					std::uint8_t raw[4];
					SampleBilinearFloat(texPixels, texW, texH, u, vv, wrapS, wrapT, raw);
					sR = raw[0]; sG = raw[1]; sB = raw[2]; sA = raw[3];
				} else if (texPixels != nullptr) {
					u = WrapUV(u, wrapS);
					vv = WrapUV(vv, wrapT);
					const std::int32_t srcX = std::max(0, std::min(texW - 1, static_cast<std::int32_t>(u * (texW - 1) + 0.5f)));
					const std::int32_t srcY = std::max(0, std::min(texH - 1, static_cast<std::int32_t>(vv * (texH - 1) + 0.5f)));
					const std::uint8_t* src = texPixels + (srcY * texW + srcX) * 4;
					sR = src[0]; sG = src[1]; sB = src[2]; sA = src[3];
				} else {
					sR = 255; sG = 255; sB = 255; sA = 255;
				}

				if DEATH_UNLIKELY(ctx.fragmentShader != nullptr) {
					std::uint8_t px4[4] = {
						static_cast<std::uint8_t>(sR), static_cast<std::uint8_t>(sG),
						static_cast<std::uint8_t>(sB), static_cast<std::uint8_t>(sA)
					};
					FragmentShaderInput fsInput;
					fsInput.rgba = px4;
					fsInput.u = u;
					fsInput.v = vv;
					fsInput.x = px;
					fsInput.y = py;
					fsInput.texWidth = texW;
					fsInput.texHeight = texH;
					fsInput.textures = ctx.textures;
					fsInput.color = ctx.ff.color;
					fsInput.userData = ctx.fragmentShaderUserData;
					ctx.fragmentShader(fsInput);
					sR = px4[0]; sG = px4[1]; sB = px4[2]; sA = px4[3];
				} else {
					sR = (sR * tR) >> 8;
					sG = (sG * tG) >> 8;
					sB = (sB * tB) >> 8;
					sA = (sA * tA) >> 8;
				}

				// Y is flipped when the destination is a render-target texture (stored bottom-up)
				const std::int32_t storeY = (g_state.isFboTarget ? (g_state.bufferHeight - 1 - py) : py);
				std::uint8_t* dstPx = g_state.colorBuffer + (static_cast<std::size_t>(storeY) * g_state.bufferWidth + px) * 4;

				if (useBlend) {
					if (sA == 0) {
						continue;
					}
					if (useFastBlend) {
						if (sA >= 255) {
							dstPx[0] = static_cast<std::uint8_t>(sR);
							dstPx[1] = static_cast<std::uint8_t>(sG);
							dstPx[2] = static_cast<std::uint8_t>(sB);
							dstPx[3] = static_cast<std::uint8_t>(sA);
						} else {
							const std::int32_t inv = 255 - sA;
							dstPx[0] = static_cast<std::uint8_t>((sR * sA + dstPx[0] * inv) >> 8);
							dstPx[1] = static_cast<std::uint8_t>((sG * sA + dstPx[1] * inv) >> 8);
							dstPx[2] = static_cast<std::uint8_t>((sB * sA + dstPx[2] * inv) >> 8);
							dstPx[3] = static_cast<std::uint8_t>((sA * 255 + dstPx[3] * inv) >> 8);
						}
					} else {
						BlendPixelGeneric(dstPx, sR, sG, sB, sA, g_state.blendSrc, g_state.blendDst);
					}
				} else {
					dstPx[0] = static_cast<std::uint8_t>(sR);
					dstPx[1] = static_cast<std::uint8_t>(sG);
					dstPx[2] = static_cast<std::uint8_t>(sB);
					dstPx[3] = static_cast<std::uint8_t>(sA);
				}
			}
		}

		void DrawPrimitive(const DrawContext& ctx, PrimitiveType type, const SmallVectorImpl<std::int32_t>& indices)
		{
			switch (type) {
				case PrimitiveType::Triangles: {
					for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
						RasterizeTriangle(ctx,
						    FetchVertex(ctx, indices[i]),
						    FetchVertex(ctx, indices[i + 1]),
						    FetchVertex(ctx, indices[i + 2]));
					}
					break;
				}
				case PrimitiveType::TriangleStrip: {
					for (std::size_t i = 0; i + 2 < indices.size(); i++) {
						if (i & 1) {
							RasterizeTriangle(ctx,
							    FetchVertex(ctx, indices[i]),
							    FetchVertex(ctx, indices[i + 2]),
							    FetchVertex(ctx, indices[i + 1]));
						} else {
							RasterizeTriangle(ctx,
							    FetchVertex(ctx, indices[i]),
							    FetchVertex(ctx, indices[i + 1]),
							    FetchVertex(ctx, indices[i + 2]));
						}
					}
					break;
				}
				case PrimitiveType::TriangleFan: {
					for (std::size_t i = 1; i + 1 < indices.size(); i++) {
						RasterizeTriangle(ctx,
						    FetchVertex(ctx, indices[0]),
						    FetchVertex(ctx, indices[i]),
						    FetchVertex(ctx, indices[i + 1]));
					}
					break;
				}
				case PrimitiveType::Lines: {
					for (std::size_t i = 0; i + 1 < indices.size(); i += 2) {
						RasterizeLine(ctx, FetchVertex(ctx, indices[i]), FetchVertex(ctx, indices[i + 1]));
					}
					break;
				}
				case PrimitiveType::LineStrip: {
					for (std::size_t i = 0; i + 1 < indices.size(); i++) {
						RasterizeLine(ctx, FetchVertex(ctx, indices[i]), FetchVertex(ctx, indices[i + 1]));
					}
					break;
				}
				case PrimitiveType::LineLoop: {
					for (std::size_t i = 0; i + 1 < indices.size(); i++) {
						RasterizeLine(ctx, FetchVertex(ctx, indices[i]), FetchVertex(ctx, indices[i + 1]));
					}
					if (indices.size() > 2) {
						RasterizeLine(ctx, FetchVertex(ctx, indices[indices.size() - 1]), FetchVertex(ctx, indices[0]));
					}
					break;
				}
				default:
					// PrimitiveType::Points stays unimplemented: nothing in the engine submits point
					// primitives (repo-wide, the only PrimitiveType::Points mentions are the backend
					// topology-mapping tables), so a point rasterizer would be dead code.
					break;
			}
		}
	}

	// =========================================================================
	// Public API
	// =========================================================================
	void SwRaster::SetColorBuffer(std::uint8_t* pixels, std::int32_t width, std::int32_t height, bool isFboTarget)
	{
		g_state.colorBuffer = pixels;
		g_state.bufferWidth = width;
		g_state.bufferHeight = height;
		g_state.isFboTarget = isFboTarget;

		// Keep the tile renderer pointed at the same surface. Initialize() is idempotent (spins up the worker
		// pool once); SetTargetBuffer() early-returns when the target is unchanged (the device calls this
		// before every draw), and flushes the previous target's queue when it actually changes.
		SwTileRenderer::Initialize();
		SwTileRenderer::SetTargetBuffer(pixels, width, height, isFboTarget);
	}

	void SwRaster::SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		g_state.viewportX = x;
		g_state.viewportY = y;
		g_state.viewportW = width;
		g_state.viewportH = height;

		// Mirror the viewport so the tile renderer snapshots it into each deferred command and reproduces the
		// exact NDC→screen transform the immediate path uses.
		SwTileRenderer::SetViewport(x, y, width, height);
	}

	void SwRaster::SetScissor(bool enabled, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		g_state.scissorEnabled = enabled;
		g_state.scissorX = x;
		g_state.scissorY = y;
		g_state.scissorW = width;
		g_state.scissorH = height;
	}

	void SwRaster::SetBlending(bool enabled, SwBlendFactor src, SwBlendFactor dst)
	{
		g_state.blendingEnabled = enabled;
		g_state.blendSrc = src;
		g_state.blendDst = dst;
	}

	void SwRaster::Clear(float r, float g, float b, float a)
	{
		if (g_state.colorBuffer == nullptr) {
			return;
		}

		// A full-buffer clear wipes everything drawn before it, so drop any deferred draws still queued for
		// this surface rather than letting them re-render on top of the cleared buffer at the next flush.
		if (SwTileRenderer::GetPendingCommandCount() > 0) {
			SwTileRenderer::DiscardPending();
		}

		const std::uint8_t rb = static_cast<std::uint8_t>(r * 255.0f);
		const std::uint8_t gb = static_cast<std::uint8_t>(g * 255.0f);
		const std::uint8_t bb = static_cast<std::uint8_t>(b * 255.0f);
		const std::uint8_t ab = static_cast<std::uint8_t>(a * 255.0f);
		const std::int32_t totalPixels = g_state.bufferWidth * g_state.bufferHeight;
		if (rb == gb && gb == bb && bb == ab) {
			// All channels identical: single memset
			std::memset(g_state.colorBuffer, rb, static_cast<std::size_t>(totalPixels) * 4);
		} else {
			// Build 32-bit RGBA pattern and fill using 32-bit writes
			const std::uint32_t pattern = static_cast<std::uint32_t>(rb)
				| (static_cast<std::uint32_t>(gb) << 8)
				| (static_cast<std::uint32_t>(bb) << 16)
				| (static_cast<std::uint32_t>(ab) << 24);
			std::uint32_t* dst32 = reinterpret_cast<std::uint32_t*>(g_state.colorBuffer);
			for (std::int32_t i = 0; i < totalPixels; ++i) {
				dst32[i] = pattern;
			}
		}
	}

	void SwRaster::SetDrawContext(const DrawContext& ctx)
	{
		static DrawContext s_ctx;
		s_ctx = ctx;
		g_state.drawCtx = &s_ctx;
		// Mirror blend / scissor state into global state so the rasterizer picks them up
		g_state.blendingEnabled = ctx.blendingEnabled;
		g_state.blendSrc = ctx.blendSrc;
		g_state.blendDst = ctx.blendDst;
		g_state.scissorEnabled = ctx.scissorEnabled;
		g_state.scissorX = ctx.scissorRect.X;
		g_state.scissorY = ctx.scissorRect.Y;
		g_state.scissorW = ctx.scissorRect.W;
		g_state.scissorH = ctx.scissorRect.H;
	}

	void SwRaster::ClearDrawContext()
	{
		g_state.drawCtx = nullptr;
	}

	void SwRaster::Draw(PrimitiveType type, std::int32_t firstVertex, std::int32_t count)
	{
		if DEATH_UNLIKELY(g_state.drawCtx == nullptr) return;

		// Fast path: detect fullscreen texture blits and handle with direct memcpy/stretch
		if (TryFastBlit(*g_state.drawCtx, type, firstVertex, count)) {
			return;
		}

		// Defer to the tile-based renderer. An accepted command is rasterized later (tiled, multi-threaded);
		// a declined one (an effect parameter block that can't be snapshotted, or the buffer being full)
		// falls through to immediate rendering below — but flush the queue first so it lands after everything
		// submitted before it, preserving draw order.
		if (SwTileRenderer::IsActive()) {
			if (SwTileRenderer::SubmitCommand(*g_state.drawCtx, type, firstVertex, count)) {
				return;
			}
			SwTileRenderer::Flush();
		}

		// Fast path: procedural 4-vertex quad (TriangleStrip, no vertex buffer)
		if DEATH_LIKELY(type == PrimitiveType::TriangleStrip && count == 4 && firstVertex == 0 &&
		    g_state.drawCtx->vertexData == nullptr) {
			const DrawContext& ctx = *g_state.drawCtx;
			Vertex2D v0 = FetchVertex(ctx, 0), v1 = FetchVertex(ctx, 1);
			Vertex2D v2 = FetchVertex(ctx, 2), v3 = FetchVertex(ctx, 3);
			// Verify axis-aligned (no rotation: same x for vertical pairs, same y for horizontal pairs)
			if DEATH_LIKELY(std::fabs(v0.x - v1.x) < 0.5f && std::fabs(v2.x - v3.x) < 0.5f &&
							std::fabs(v0.y - v2.y) < 0.5f && std::fabs(v1.y - v3.y) < 0.5f) {
				DrawAxisAlignedQuad(ctx, v0, v1, v2, v3);
				return;
			}
			// Rotated/scaled quad: use affine scanline rasterizer
			DrawAffineQuad(ctx, v0, v1, v2, v3);
			return;
		}

		SmallVector<std::int32_t> indices(static_cast<std::size_t>(count));
		for (std::int32_t i = 0; i < count; ++i) {
			indices[i] = firstVertex + i;
		}
		DrawPrimitive(*g_state.drawCtx, type, indices);
	}

	void SwRaster::Flush()
	{
		SwTileRenderer::Flush();
	}
}

#endif
