#include "LightingRenderer.h"
#include "PlayerViewport.h"
#include "../PreferencesCache.h"

#include "../../nCine/Graphics/RenderQueue.h"

#include <Cpu.h>
#include <cmath>
#include <algorithm>
#include <cstring>

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

using namespace Death;

namespace Jazz2::Rendering
{
	LightingRenderer::LightingRenderer(PlayerViewport* owner)
		: _owner(owner)
#if defined(RHI_CAP_SHADERS) && defined(RHI_CAP_FRAMEBUFFERS)
			, _renderCommandsCount(0)
#endif
	{
		_emittedLightsCache.reserve(32);
		setVisitOrderState(SceneNode::VisitOrderState::Disabled);
	}

#if !defined(RHI_CAP_SHADERS) || !defined(RHI_CAP_FRAMEBUFFERS)
	namespace
	{
		// =====================================================================
		// CPU-dispatched lighting scanline renderer
		// Renders a single light's contribution into a horizontal span of pixels.
		// =====================================================================
		extern void DEATH_CPU_DISPATCHED_DECLARATION(renderLightScanline)(
			std::uint8_t* DEATH_RESTRICT row, std::int32_t count, float dxStart, float dxStep, float dy2Norm,
			float nearDiv, bool nearFull, float intensityScaled, float brightnessScaled);
		DEATH_CPU_DISPATCHER_DECLARATION(renderLightScanline)

		// Scalar fallback
		DEATH_CPU_MAYBE_UNUSED typename std::decay<decltype(renderLightScanline)>::type renderLightScanlineImplementation(Cpu::ScalarT) {
			return [](std::uint8_t* DEATH_RESTRICT row, std::int32_t count, float dxStart, float dxStep, float dy2Norm,
					  float nearDiv, bool nearFull, float intensityScaled, float brightnessScaled) {
				float dx = dxStart;
				for (std::int32_t i = 0; i < count; i++, row += 4, dx += dxStep) {
					float dist2 = dx * dx + dy2Norm;
					if (dist2 > 1.0f) continue;

					// Fast approximate sqrt via integer rsqrt trick
					float dist;
					if (dist2 < 1e-10f) {
						dist = 0.0f;
					} else {
						float x = dist2;
						std::int32_t bits;
						std::memcpy(&bits, &x, 4);
						bits = 0x5F375A86 - (bits >> 1);
						std::memcpy(&x, &bits, 4);
						x = x * (1.5f - 0.5f * dist2 * x * x);
						dist = dist2 * x;
					}

					float t;
					if (nearFull) {
						t = 1.0f;
					} else {
						t = (1.0f - dist) * nearDiv;
						if (t > 1.0f) t = 1.0f;
						else if (t < 0.0f) t = 0.0f;
					}
					float strength = t * t * t;

					std::int32_t addR = (std::int32_t)(strength * intensityScaled + 0.5f);
					std::int32_t addG = (std::int32_t)(strength * brightnessScaled + 0.5f);
					if (addR > 0) {
						std::int32_t n = row[0] + addR;
						row[0] = (std::uint8_t)(n > 255 ? 255 : n);
					}
					if (addG > 0) {
						std::int32_t n = row[1] + addG;
						row[1] = (std::uint8_t)(n > 255 ? 255 : n);
					}
				}
			};
		}

#if defined(DEATH_ENABLE_SSE2)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_SSE2 typename std::decay<decltype(renderLightScanline)>::type renderLightScanlineImplementation(Cpu::Sse2T) {
			return [](std::uint8_t* DEATH_RESTRICT row, std::int32_t count, float dxStart, float dxStep, float dy2Norm,
					  float nearDiv, bool nearFull, float intensityScaled, float brightnessScaled) DEATH_ENABLE_SSE2 {
				const __m128 vDy2 = _mm_set1_ps(dy2Norm);
				const __m128 vOne = _mm_set1_ps(1.0f);
				const __m128 vZero = _mm_setzero_ps();
				const __m128 vIntensity = _mm_set1_ps(intensityScaled);
				const __m128 vBrightness = _mm_set1_ps(brightnessScaled);
				const __m128 vHalf = _mm_set1_ps(0.5f);
				const __m128 vNearDiv = _mm_set1_ps(nearDiv);
				const __m128 vEps = _mm_set1_ps(1e-10f);
				const __m128 vStep4 = _mm_set1_ps(dxStep * 4.0f);

				__m128 vDx = _mm_add_ps(_mm_set1_ps(dxStart),
					_mm_mul_ps(_mm_set_ps(3.0f, 2.0f, 1.0f, 0.0f), _mm_set1_ps(dxStep)));

				std::int32_t i = 0;
				for (; i + 4 <= count; i += 4, row += 16) {
					__m128 vDx2 = _mm_mul_ps(vDx, vDx);
					__m128 vDist2 = _mm_add_ps(vDx2, vDy2);

					__m128 mask = _mm_cmple_ps(vDist2, vOne);
					if (_mm_movemask_ps(mask) == 0) {
						vDx = _mm_add_ps(vDx, vStep4);
						continue;
					}

					// dist ≈ dist² * rsqrt(dist²)
					__m128 vDist2Safe = _mm_max_ps(vDist2, vEps);
					__m128 vRsqrt = _mm_rsqrt_ps(vDist2Safe);
					__m128 vDist = _mm_mul_ps(vDist2Safe, vRsqrt);

					// t = clamp((1 - dist) * nearDiv, 0, 1)
					__m128 vT;
					if (nearFull) {
						vT = vOne;
					} else {
						vT = _mm_mul_ps(_mm_sub_ps(vOne, vDist), vNearDiv);
						vT = _mm_min_ps(_mm_max_ps(vT, vZero), vOne);
					}

					// strength = t³, masked to circle
					__m128 vT2 = _mm_mul_ps(vT, vT);
					__m128 vStrength = _mm_and_ps(_mm_mul_ps(vT2, vT), mask);

					// Compute additions
					__m128i iAddR = _mm_cvttps_epi32(_mm_add_ps(_mm_mul_ps(vStrength, vIntensity), vHalf));
					__m128i iAddG = _mm_cvttps_epi32(_mm_add_ps(_mm_mul_ps(vStrength, vBrightness), vHalf));

					// Saturating add to pixel R and G channels
					alignas(16) std::int32_t addR[4], addG[4];
					_mm_store_si128(reinterpret_cast<__m128i*>(addR), iAddR);
					_mm_store_si128(reinterpret_cast<__m128i*>(addG), iAddG);

					for (std::int32_t k = 0; k < 4; k++) {
						if (addR[k] | addG[k]) {
							std::int32_t nR = row[k * 4 + 0] + addR[k];
							std::int32_t nG = row[k * 4 + 1] + addG[k];
							row[k * 4 + 0] = (std::uint8_t)(nR > 255 ? 255 : nR);
							row[k * 4 + 1] = (std::uint8_t)(nG > 255 ? 255 : nG);
						}
					}

					vDx = _mm_add_ps(vDx, vStep4);
				}

				// Scalar tail using SSE rsqrt for single values
				float dx = dxStart + i * dxStep;
				for (; i < count; i++, row += 4, dx += dxStep) {
					float dist2 = dx * dx + dy2Norm;
					if (dist2 > 1.0f) continue;
					float invSqrt = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(std::max(dist2, 1e-10f))));
					float dist = dist2 * invSqrt;
					float t = nearFull ? 1.0f : std::min(std::max((1.0f - dist) * nearDiv, 0.0f), 1.0f);
					float s = t * t * t;
					std::int32_t aR = (std::int32_t)(s * intensityScaled + 0.5f);
					std::int32_t aG = (std::int32_t)(s * brightnessScaled + 0.5f);
					if (aR > 0) { std::int32_t n = row[0] + aR; row[0] = (std::uint8_t)(n > 255 ? 255 : n); }
					if (aG > 0) { std::int32_t n = row[1] + aG; row[1] = (std::uint8_t)(n > 255 ? 255 : n); }
				}
			};
		}
#endif

#if defined(DEATH_ENABLE_AVX2)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_AVX2 typename std::decay<decltype(renderLightScanline)>::type renderLightScanlineImplementation(Cpu::Avx2T) {
			return [](std::uint8_t* DEATH_RESTRICT row, std::int32_t count, float dxStart, float dxStep, float dy2Norm,
					  float nearDiv, bool nearFull, float intensityScaled, float brightnessScaled) DEATH_ENABLE_AVX2 {
				const __m256 vDy2 = _mm256_set1_ps(dy2Norm);
				const __m256 vOne = _mm256_set1_ps(1.0f);
				const __m256 vZero = _mm256_setzero_ps();
				const __m256 vIntensity = _mm256_set1_ps(intensityScaled);
				const __m256 vBrightness = _mm256_set1_ps(brightnessScaled);
				const __m256 vHalf = _mm256_set1_ps(0.5f);
				const __m256 vNearDiv = _mm256_set1_ps(nearDiv);
				const __m256 vEps = _mm256_set1_ps(1e-10f);
				const __m256 vStep8 = _mm256_set1_ps(dxStep * 8.0f);

				__m256 vDx = _mm256_add_ps(_mm256_set1_ps(dxStart),
					_mm256_mul_ps(_mm256_set_ps(7.0f, 6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f, 0.0f), _mm256_set1_ps(dxStep)));

				std::int32_t i = 0;
				for (; i + 8 <= count; i += 8, row += 32) {
					__m256 vDx2 = _mm256_mul_ps(vDx, vDx);
					__m256 vDist2 = _mm256_add_ps(vDx2, vDy2);

					__m256 mask = _mm256_cmp_ps(vDist2, vOne, _CMP_LE_OQ);
					if (_mm256_movemask_ps(mask) == 0) {
						vDx = _mm256_add_ps(vDx, vStep8);
						continue;
					}

					__m256 vDist2Safe = _mm256_max_ps(vDist2, vEps);
					__m256 vRsqrt = _mm256_rsqrt_ps(vDist2Safe);
					__m256 vDist = _mm256_mul_ps(vDist2Safe, vRsqrt);

					__m256 vT;
					if (nearFull) {
						vT = vOne;
					} else {
						vT = _mm256_mul_ps(_mm256_sub_ps(vOne, vDist), vNearDiv);
						vT = _mm256_min_ps(_mm256_max_ps(vT, vZero), vOne);
					}

					__m256 vT2 = _mm256_mul_ps(vT, vT);
					__m256 vStrength = _mm256_and_ps(_mm256_mul_ps(vT2, vT), mask);

					__m256i iAddR = _mm256_cvttps_epi32(_mm256_add_ps(_mm256_mul_ps(vStrength, vIntensity), vHalf));
					__m256i iAddG = _mm256_cvttps_epi32(_mm256_add_ps(_mm256_mul_ps(vStrength, vBrightness), vHalf));

					alignas(32) std::int32_t addR[8], addG[8];
					_mm256_store_si256(reinterpret_cast<__m256i*>(addR), iAddR);
					_mm256_store_si256(reinterpret_cast<__m256i*>(addG), iAddG);

					for (std::int32_t k = 0; k < 8; k++) {
						if (addR[k] | addG[k]) {
							std::int32_t nR = row[k * 4 + 0] + addR[k];
							std::int32_t nG = row[k * 4 + 1] + addG[k];
							row[k * 4 + 0] = (std::uint8_t)(nR > 255 ? 255 : nR);
							row[k * 4 + 1] = (std::uint8_t)(nG > 255 ? 255 : nG);
						}
					}

					vDx = _mm256_add_ps(vDx, vStep8);
				}

				// SSE2 tail for remaining pixels
				const __m128 vDy2_128 = _mm_set1_ps(dy2Norm);
				const __m128 vOne_128 = _mm_set1_ps(1.0f);
				const __m128 vZero_128 = _mm_setzero_ps();
				const __m128 vNearDiv_128 = _mm_set1_ps(nearDiv);
				const __m128 vEps_128 = _mm_set1_ps(1e-10f);
				const __m128 vIntensity_128 = _mm_set1_ps(intensityScaled);
				const __m128 vBrightness_128 = _mm_set1_ps(brightnessScaled);
				const __m128 vHalf_128 = _mm_set1_ps(0.5f);

				for (; i + 4 <= count; i += 4, row += 16) {
					float dx = dxStart + i * dxStep;
					__m128 vDxT = _mm_add_ps(_mm_set1_ps(dx), _mm_mul_ps(_mm_set_ps(3.0f, 2.0f, 1.0f, 0.0f), _mm_set1_ps(dxStep)));
					__m128 vDist2T = _mm_add_ps(_mm_mul_ps(vDxT, vDxT), vDy2_128);
					__m128 maskT = _mm_cmple_ps(vDist2T, vOne_128);
					if (_mm_movemask_ps(maskT) == 0) continue;

					__m128 vDist2S = _mm_max_ps(vDist2T, vEps_128);
					__m128 vRsqrtT = _mm_rsqrt_ps(vDist2S);
					__m128 vDistT = _mm_mul_ps(vDist2S, vRsqrtT);
					__m128 vTT = nearFull ? vOne_128 : _mm_min_ps(_mm_max_ps(_mm_mul_ps(_mm_sub_ps(vOne_128, vDistT), vNearDiv_128), vZero_128), vOne_128);
					__m128 vST = _mm_and_ps(_mm_mul_ps(_mm_mul_ps(vTT, vTT), vTT), maskT);

					__m128i iAR = _mm_cvttps_epi32(_mm_add_ps(_mm_mul_ps(vST, vIntensity_128), vHalf_128));
					__m128i iAG = _mm_cvttps_epi32(_mm_add_ps(_mm_mul_ps(vST, vBrightness_128), vHalf_128));

					alignas(16) std::int32_t aR[4], aG[4];
					_mm_store_si128(reinterpret_cast<__m128i*>(aR), iAR);
					_mm_store_si128(reinterpret_cast<__m128i*>(aG), iAG);
					for (std::int32_t k = 0; k < 4; k++) {
						if (aR[k] | aG[k]) {
							std::int32_t nR = row[k * 4 + 0] + aR[k];
							std::int32_t nG = row[k * 4 + 1] + aG[k];
							row[k * 4 + 0] = (std::uint8_t)(nR > 255 ? 255 : nR);
							row[k * 4 + 1] = (std::uint8_t)(nG > 255 ? 255 : nG);
						}
					}
				}

				// Scalar tail
				float dx = dxStart + i * dxStep;
				for (; i < count; i++, row += 4, dx += dxStep) {
					float dist2 = dx * dx + dy2Norm;
					if (dist2 > 1.0f) continue;
					float invS = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(std::max(dist2, 1e-10f))));
					float dist = dist2 * invS;
					float t = nearFull ? 1.0f : std::min(std::max((1.0f - dist) * nearDiv, 0.0f), 1.0f);
					float s = t * t * t;
					std::int32_t aR = (std::int32_t)(s * intensityScaled + 0.5f);
					std::int32_t aG = (std::int32_t)(s * brightnessScaled + 0.5f);
					if (aR > 0) { std::int32_t n = row[0] + aR; row[0] = (std::uint8_t)(n > 255 ? 255 : n); }
					if (aG > 0) { std::int32_t n = row[1] + aG; row[1] = (std::uint8_t)(n > 255 ? 255 : n); }
				}
			};
		}
#endif

#if defined(DEATH_ENABLE_NEON)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_NEON typename std::decay<decltype(renderLightScanline)>::type renderLightScanlineImplementation(Cpu::NeonT) {
			return [](std::uint8_t* DEATH_RESTRICT row, std::int32_t count, float dxStart, float dxStep, float dy2Norm,
					  float nearDiv, bool nearFull, float intensityScaled, float brightnessScaled) DEATH_ENABLE_NEON {
				const float32x4_t vDy2 = vdupq_n_f32(dy2Norm);
				const float32x4_t vOne = vdupq_n_f32(1.0f);
				const float32x4_t vZero = vdupq_n_f32(0.0f);
				const float32x4_t vNearDiv = vdupq_n_f32(nearDiv);
				const float32x4_t vEps = vdupq_n_f32(1e-10f);
				const float32x4_t vStep4 = vdupq_n_f32(dxStep * 4.0f);
				const float offsets[4] = { 0.0f, 1.0f, 2.0f, 3.0f };

				float32x4_t vDx = vaddq_f32(vdupq_n_f32(dxStart),
					vmulq_f32(vld1q_f32(offsets), vdupq_n_f32(dxStep)));

				std::int32_t i = 0;
				for (; i + 4 <= count; i += 4, row += 16) {
					float32x4_t vDx2 = vmulq_f32(vDx, vDx);
					float32x4_t vDist2 = vaddq_f32(vDx2, vDy2);

					uint32x4_t mask = vcleq_f32(vDist2, vOne);
#	if defined(DEATH_TARGET_32BIT)
					uint32x2_t maskFolded_ = vorr_u32(vget_low_u32(mask), vget_high_u32(mask));
					if (!(vget_lane_u32(maskFolded_, 0) | vget_lane_u32(maskFolded_, 1))) {
#	else
					if (vmaxvq_u32(mask) == 0) {
#	endif
						vDx = vaddq_f32(vDx, vStep4);
						continue;
					}

					// dist ≈ dist² * vrsqrte with one Newton-Raphson refinement
					float32x4_t vDist2Safe = vmaxq_f32(vDist2, vEps);
					float32x4_t vRsqrt = vrsqrteq_f32(vDist2Safe);
					vRsqrt = vmulq_f32(vrsqrtsq_f32(vmulq_f32(vDist2Safe, vRsqrt), vRsqrt), vRsqrt);
					float32x4_t vDist = vmulq_f32(vDist2Safe, vRsqrt);

					float32x4_t vT;
					if (nearFull) {
						vT = vOne;
					} else {
						vT = vmulq_f32(vsubq_f32(vOne, vDist), vNearDiv);
						vT = vminq_f32(vmaxq_f32(vT, vZero), vOne);
					}

					float32x4_t vT2 = vmulq_f32(vT, vT);
					float32x4_t vStrength = vbslq_f32(mask, vmulq_f32(vT2, vT), vZero);

					alignas(16) float strengthArr[4];
					vst1q_f32(strengthArr, vStrength);

					for (std::int32_t k = 0; k < 4; k++) {
						if (strengthArr[k] > 0.0f) {
							std::int32_t aR = (std::int32_t)(strengthArr[k] * intensityScaled + 0.5f);
							std::int32_t aG = (std::int32_t)(strengthArr[k] * brightnessScaled + 0.5f);
							if (aR > 0) { std::int32_t n = row[k * 4 + 0] + aR; row[k * 4 + 0] = (std::uint8_t)(n > 255 ? 255 : n); }
							if (aG > 0) { std::int32_t n = row[k * 4 + 1] + aG; row[k * 4 + 1] = (std::uint8_t)(n > 255 ? 255 : n); }
						}
					}

					vDx = vaddq_f32(vDx, vStep4);
				}

				// Scalar tail
				float dx = dxStart + i * dxStep;
				for (; i < count; i++, row += 4, dx += dxStep) {
					float dist2 = dx * dx + dy2Norm;
					if (dist2 > 1.0f) continue;
					float safe = std::max(dist2, 1e-10f);
					float rsq = vgetq_lane_f32(vrsqrteq_f32(vdupq_n_f32(safe)), 0);
					rsq = rsq * (1.5f - 0.5f * safe * rsq * rsq);
					float dist = safe * rsq;
					float t = nearFull ? 1.0f : std::min(std::max((1.0f - dist) * nearDiv, 0.0f), 1.0f);
					float s = t * t * t;
					std::int32_t aR = (std::int32_t)(s * intensityScaled + 0.5f);
					std::int32_t aG = (std::int32_t)(s * brightnessScaled + 0.5f);
					if (aR > 0) { std::int32_t n = row[0] + aR; row[0] = (std::uint8_t)(n > 255 ? 255 : n); }
					if (aG > 0) { std::int32_t n = row[1] + aG; row[1] = (std::uint8_t)(n > 255 ? 255 : n); }
				}
			};
		}
#endif

#if defined(DEATH_ENABLE_SIMD128)
		DEATH_CPU_MAYBE_UNUSED DEATH_ENABLE_SIMD128 typename std::decay<decltype(renderLightScanline)>::type renderLightScanlineImplementation(Cpu::Simd128T) {
			return [](std::uint8_t* DEATH_RESTRICT row, std::int32_t count,
					  float dxStart, float dxStep, float dy2Norm,
					  float nearDiv, bool nearFull,
					  float intensityScaled, float brightnessScaled) DEATH_ENABLE_SIMD128 {
				const v128_t vDy2 = wasm_f32x4_splat(dy2Norm);
				const v128_t vOne = wasm_f32x4_splat(1.0f);
				const v128_t vZero = wasm_f32x4_splat(0.0f);
				const v128_t vNearDiv = wasm_f32x4_splat(nearDiv);
				const v128_t vEps = wasm_f32x4_splat(1e-10f);
				const v128_t vStep4 = wasm_f32x4_splat(dxStep * 4.0f);

				v128_t vDx = wasm_f32x4_add(wasm_f32x4_splat(dxStart),
					wasm_f32x4_mul(wasm_f32x4_make(0.0f, 1.0f, 2.0f, 3.0f), wasm_f32x4_splat(dxStep)));

				std::int32_t i = 0;
				for (; i + 4 <= count; i += 4, row += 16) {
					v128_t vDx2 = wasm_f32x4_mul(vDx, vDx);
					v128_t vDist2 = wasm_f32x4_add(vDx2, vDy2);

					v128_t mask = wasm_f32x4_le(vDist2, vOne);
					if (!wasm_v128_any_true(mask)) {
						vDx = wasm_f32x4_add(vDx, vStep4);
						continue;
					}

					v128_t vDist2Safe = wasm_f32x4_max(vDist2, vEps);
					v128_t vSqrt = wasm_f32x4_sqrt(vDist2Safe);

					v128_t vT;
					if (nearFull) {
						vT = vOne;
					} else {
						vT = wasm_f32x4_mul(wasm_f32x4_sub(vOne, vSqrt), vNearDiv);
						vT = wasm_f32x4_min(wasm_f32x4_max(vT, vZero), vOne);
					}

					v128_t vT2 = wasm_f32x4_mul(vT, vT);
					v128_t vStrength = wasm_v128_and(wasm_f32x4_mul(vT2, vT), mask);

					alignas(16) float strengthArr[4];
					wasm_v128_store(strengthArr, vStrength);

					for (std::int32_t k = 0; k < 4; k++) {
						if (strengthArr[k] > 0.0f) {
							std::int32_t aR = (std::int32_t)(strengthArr[k] * intensityScaled + 0.5f);
							std::int32_t aG = (std::int32_t)(strengthArr[k] * brightnessScaled + 0.5f);
							if (aR > 0) { std::int32_t n = row[k * 4 + 0] + aR; row[k * 4 + 0] = (std::uint8_t)(n > 255 ? 255 : n); }
							if (aG > 0) { std::int32_t n = row[k * 4 + 1] + aG; row[k * 4 + 1] = (std::uint8_t)(n > 255 ? 255 : n); }
						}
					}

					vDx = wasm_f32x4_add(vDx, vStep4);
				}

				// Scalar tail
				float dx = dxStart + i * dxStep;
				for (; i < count; i++, row += 4, dx += dxStep) {
					float dist2 = dx * dx + dy2Norm;
					if (dist2 > 1.0f) continue;
					float dist = std::sqrt(std::max(dist2, 1e-10f));
					float t = nearFull ? 1.0f : std::min(std::max((1.0f - dist) * nearDiv, 0.0f), 1.0f);
					float s = t * t * t;
					std::int32_t aR = (std::int32_t)(s * intensityScaled + 0.5f);
					std::int32_t aG = (std::int32_t)(s * brightnessScaled + 0.5f);
					if (aR > 0) { std::int32_t n = row[0] + aR; row[0] = (std::uint8_t)(n > 255 ? 255 : n); }
					if (aG > 0) { std::int32_t n = row[1] + aG; row[1] = (std::uint8_t)(n > 255 ? 255 : n); }
				}
			};
		}
#endif

		DEATH_CPU_DISPATCHER_BASE(renderLightScanlineImplementation)
		DEATH_CPU_DISPATCHED(renderLightScanlineImplementation, void DEATH_CPU_DISPATCHED_DECLARATION(renderLightScanline)(
				std::uint8_t* DEATH_RESTRICT row, std::int32_t count, float dxStart, float dxStep, float dy2Norm,
				float nearDiv, bool nearFull, float intensityScaled, float brightnessScaled))({
			return renderLightScanlineImplementation(Cpu::DefaultBase)(row, count, dxStart, dxStep, dy2Norm, nearDiv, nearFull, intensityScaled, brightnessScaled);
		})
	}
#endif

	bool LightingRenderer::OnDraw(RenderQueue& renderQueue)
	{
#if !defined(RHI_CAP_SHADERS) || !defined(RHI_CAP_FRAMEBUFFERS)
		// SW path: render lights directly into _lightingBuffer pixels
		_emittedLightsCache.clear();

		auto actors = _owner->_levelHandler->GetActors();
		std::size_t actorsCount = actors.size();
		for (std::size_t i = 0; i < actorsCount; i++) {
			actors[i]->OnEmitLights(_emittedLightsCache);
		}

		auto* lightTex = _owner->_lightingBuffer.get();
		lightTex->EnsureRenderTarget();
		std::uint8_t* pixels = lightTex->GetMutablePixels(0);
		if (pixels == nullptr) {
			return true;
		}

		std::int32_t texW = lightTex->GetWidth();
		std::int32_t texH = lightTex->GetHeight();

		// Ambient clear color
		float ambient = _owner->_ambientLight.W;
		std::uint8_t ambientR = (std::uint8_t)std::min((std::int32_t)(ambient * 255.0f + 0.5f), 255);
		std::uint32_t clearPixel = ambientR | (0x00u << 8) | (0x00u << 16) | (0xFFu << 24);

		// Resolution scale factor: world pixels → lighting buffer pixels
		float resScale = (float)PreferencesCache::LightingResolutionPercent / 100.0f;

		// Camera transform: world position to lighting buffer pixel coordinates
		float halfViewW = (float)texW * 0.5f;
		float halfViewH = (float)texH * 0.5f;
		float camOriginX = _owner->_cameraPos.X * resScale - halfViewW;
		float camOriginY = _owner->_cameraPos.Y * resScale - halfViewH;

		// Precompute per-light data (bounding boxes, parameters)
		struct LightData {
			float cx, cy, radiusFar, invRadiusFar;
			float nearDiv, intensityScaled, brightnessScaled;
			std::int32_t x0, y0, x1, y1;
			bool nearFull;
		};

		SmallVector<LightData, 64> lightDataCache;
		lightDataCache.reserve(_emittedLightsCache.size());

		for (auto& light : _emittedLightsCache) {
			float radiusFar = light.RadiusFar * resScale;
			if (radiusFar <= 0.0f) continue;

			float intensity = std::max(light.Intensity, 0.0f);
			float brightness = std::max(light.Brightness, 0.0f);
			if (intensity <= 0.0f && brightness <= 0.0f) continue;

			float cx = light.Pos.X * resScale - camOriginX;
			float cy = light.Pos.Y * resScale - camOriginY;

			if (cx + radiusFar < 0.0f || cx - radiusFar >= (float)texW ||
				cy + radiusFar < 0.0f || cy - radiusFar >= (float)texH) {
				continue;
			}

			LightData ld;
			ld.cx = cx;
			ld.cy = cy;
			ld.radiusFar = radiusFar;
			ld.invRadiusFar = 1.0f / radiusFar;
			ld.x0 = std::max((std::int32_t)(cx - radiusFar), 0);
			ld.y0 = std::max((std::int32_t)(cy - radiusFar), 0);
			ld.x1 = std::min((std::int32_t)(cx + radiusFar + 1.0f), texW);
			ld.y1 = std::min((std::int32_t)(cy + radiusFar + 1.0f), texH);

			if (ld.x0 >= ld.x1 || ld.y0 >= ld.y1) continue;

			float radiusNearRatio = light.RadiusNear / light.RadiusFar;
			ld.nearFull = (radiusNearRatio >= 1.0f);
			ld.nearDiv = ld.nearFull ? 1.0f : 1.0f / (1.0f - radiusNearRatio);
			ld.intensityScaled = intensity * 255.0f;
			ld.brightnessScaled = brightness * 255.0f;
			lightDataCache.push_back(ld);
		}

		// Process lighting in 32x32 tile blocks for L1 cache efficiency
		static constexpr std::int32_t LightTileSize = 32;

		std::int32_t tilesX = (texW + LightTileSize - 1) / LightTileSize;
		std::int32_t tilesY = (texH + LightTileSize - 1) / LightTileSize;

		for (std::int32_t tileRow = 0; tileRow < tilesY; tileRow++) {
			std::int32_t tileY0 = tileRow * LightTileSize;
			std::int32_t tileY1 = std::min(tileY0 + LightTileSize, texH);

			for (std::int32_t tileCol = 0; tileCol < tilesX; tileCol++) {
				std::int32_t tileX0 = tileCol * LightTileSize;
				std::int32_t tileX1 = std::min(tileX0 + LightTileSize, texW);

				// Clear this tile with ambient color
				for (std::int32_t py = tileY0; py < tileY1; py++) {
					std::uint32_t* row32 = reinterpret_cast<std::uint32_t*>(pixels + py * texW * 4) + tileX0;
					std::int32_t rowPixels = tileX1 - tileX0;
					for (std::int32_t px = 0; px < rowPixels; px++) {
						row32[px] = clearPixel;
					}
				}

				// Render all lights that overlap this tile
				for (const auto& ld : lightDataCache) {
					if (ld.x0 >= tileX1 || ld.x1 <= tileX0 ||
						ld.y0 >= tileY1 || ld.y1 <= tileY0) {
						continue;
					}

					std::int32_t ly0 = std::max(ld.y0, tileY0);
					std::int32_t ly1 = std::min(ld.y1, tileY1);

					for (std::int32_t py = ly0; py < ly1; py++) {
						float dy = (float)py + 0.5f - ld.cy;
						float dyNorm = dy * ld.invRadiusFar;
						float dy2Norm = dyNorm * dyNorm;

						if (dy2Norm > 1.0f) continue;

						float maxDxNorm = std::sqrt(1.0f - dy2Norm);
						float maxDxAbs = ld.radiusFar * maxDxNorm;
						std::int32_t rowX0 = std::max(std::max(ld.x0, (std::int32_t)(ld.cx - maxDxAbs)), tileX0);
						std::int32_t rowX1 = std::min(std::min(ld.x1, (std::int32_t)(ld.cx + maxDxAbs + 1.0f)), tileX1);
						std::int32_t rowCount = rowX1 - rowX0;
						if (rowCount <= 0) continue;

						std::uint8_t* row = pixels + (py * texW + rowX0) * 4;
						float dxStart = ((float)rowX0 + 0.5f - ld.cx) * ld.invRadiusFar;
						float dxStep = ld.invRadiusFar;

						renderLightScanline(row, rowCount, dxStart, dxStep, dy2Norm,
							ld.nearDiv, ld.nearFull, ld.intensityScaled, ld.brightnessScaled);
					}
				}
			}
		}

		return true;
#else
		_renderCommandsCount = 0;
		_emittedLightsCache.clear();

		// Collect all active light emitters
		auto actors = _owner->_levelHandler->GetActors();
		std::size_t actorsCount = actors.size();
		for (std::size_t i = 0; i < actorsCount; i++) {
			actors[i]->OnEmitLights(_emittedLightsCache);
		}

		for (auto& light : _emittedLightsCache) {
			auto command = RentRenderCommand();
			auto instanceBlock = command->GetMaterial().UniformBlock(Material::InstanceBlockName);
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(light.Pos.X, light.Pos.Y, light.RadiusNear / light.RadiusFar, 0.0f);
			instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(light.RadiusFar * 2.0f, light.RadiusFar * 2.0f);
			instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatValue(light.Intensity, light.Brightness, 0.0f, 0.0f);
			command->SetTransformation(Matrix4x4f::Translation(light.Pos.X, light.Pos.Y, 0));

			renderQueue.AddCommand(command);
		}

		return true;
#endif
	}

#if defined(RHI_CAP_SHADERS) && defined(RHI_CAP_FRAMEBUFFERS)
	RenderCommand* LightingRenderer::RentRenderCommand()
	{
		if (_renderCommandsCount < _renderCommands.size()) {
			RenderCommand* command = _renderCommands[_renderCommandsCount].get();
			_renderCommandsCount++;
			return command;
		} else {
			std::unique_ptr<RenderCommand>& command = _renderCommands.emplace_back(std::make_unique<RenderCommand>(RenderCommand::Type::Lighting));
			_renderCommandsCount++;
			command->GetMaterial().SetShader(_owner->_levelHandler->_lightingShader);
			command->GetMaterial().SetBlendingEnabled(true);
			command->GetMaterial().SetBlendingFactors(RHI::BlendFactor::SrcAlpha, RHI::BlendFactor::One);
			command->GetMaterial().ReserveUniformsDataMemory();
			command->GetGeometry().SetDrawParameters(RHI::PrimitiveType::TriangleStrip, 0, 4);

			auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
			return command.get();
		}
	}
#endif
}
