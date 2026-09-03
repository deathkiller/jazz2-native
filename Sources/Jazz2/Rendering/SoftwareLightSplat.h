#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Jazz2::Rendering::SoftwareLighting
{
	/**
	 * @brief Reciprocal square root estimate of the platform, where it has one worth using
	 *
	 * The falloff needs one square root per texel of the lit annulus, and on the consoles without a
	 * pipelined one that is most of the splat: the Gekko/Broadway have no `fsqrt` at all, so `std::sqrt`
	 * is a software routine of a few hundred cycles, and the SH-4's `fsqrt` takes about 22 cycles and
	 * blocks the pipeline. Both do have a fast estimate of `1/sqrt(x)` - `frsqrte` (the PowerPC
	 * architecture guarantees only 1 part in 32, so it is refined with two Newton steps, which lands
	 * within about 3e-6 from any estimate at least that good) and `fsrra` (accurate to the last bits on
	 * its own). The lightmap is quantized to 8 bits per channel before anything sees it, so a relative
	 * error of that size cannot change a single output value except at a quantization boundary.
	 */
#if defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE) || defined(JAZZ2_SPLAT_TEST_RSQRTE)
	inline float RsqrtEstimate(float x)
	{
#	if defined(JAZZ2_SPLAT_TEST_RSQRTE)
		// Host harness only: an estimate deliberately as poor as the architecture allows (±1/32)
		return (1.0f / std::sqrt(x)) * (1.0f + 0.03125f * std::sin(x * 1000.0f));
#	else
		double estimate;
		asm("frsqrte %0, %1" : "=f"(estimate) : "f"((double)x));
		return (float)estimate;
#	endif
	}
#endif

	/** @brief Returns @f$ \sqrt{d2} @f$ for a squared distance, as cheaply as the platform allows */
	inline float Distance(float d2)
	{
#if defined(DEATH_TARGET_DREAMCAST)
		// `fsrra` of zero is infinity and `0 * inf` is NaN; a squared distance this small is the light's own
		// texel, where `d2 * r` then comes out as ~1e-6 and the strength as 1, exactly as it should
		float r = std::max(d2, 1.0e-12f);
		asm("fsrra %0" : "+f"(r));
		return d2 * r;
#elif defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE) || defined(JAZZ2_SPLAT_TEST_RSQRTE)
		const float x = std::max(d2, 1.0e-12f);
		float r = RsqrtEstimate(x);
		r = r * (1.5f - 0.5f * x * r * r);
		r = r * (1.5f - 0.5f * x * r * r);
		return d2 * r;
#else
		return std::sqrt(d2);
#endif
	}

	/**
	 * @brief Adds one light to a two-channel (intensity, brightness) float lightmap
	 *
	 * @param lightmap        `lmW * lmH` texels of two floats each, row-major
	 * @param cx, cy          Light centre in lightmap texels
	 * @param rLm             Far radius in lightmap texels (at least 0.5)
	 * @param radiusNearNorm  Near radius as a fraction of the far radius (may exceed 1: no falloff)
	 *
	 * The falloff is the one in `LightingFs.inc`: strength is 1 out to the near radius and then
	 * `((1 - dist) / (1 - near))^3` to the far radius, in units of the far radius. Instead of visiting the
	 * whole bounding box and classifying every texel with two compares and a branch, each row is cut
	 * analytically into the texels inside the disc - `|x - cx| <= sqrt(1 - dy^2) * rLm` - and the flat core
	 * inside those - `|x - cx| <= sqrt(near^2 - dy^2) * rLm`. Texels outside the disc are never touched, core
	 * texels get two additions, and only the annulus pays for a square root. It is the same set of texels
	 * the per-texel tests select, up to floating-point rounding of a texel exactly on a boundary, where the
	 * strength is 0 or 1 to within ~1e-7 either way - except for a light with no falloff (near >= far), whose
	 * edge is a hard step and so lands one texel differently now and then; a host harness over 196 million
	 * texel values found no other difference above 1e-3. The annulus loops are branch-free so that the compiler
	 * can interleave several texels for the in-order cores this runs on; `dx` is recomputed from `x` rather
	 * than accumulated, so no iteration depends on the previous one.
	 */
	inline void SplatLight(float* lightmap, std::int32_t lmW, std::int32_t lmH, float cx, float cy, float rLm,
		float radiusNearNorm, float intensity, float brightness)
	{
		const float invRLm = 1.0f / rLm;
		const float denom = (1.0f - radiusNearNorm);
		// A light without falloff (near >= far) has no annulus at all: the core test below covers the whole disc
		const float invDenom = (denom > 0.0f ? 1.0f / denom : 0.0f);
		const float nearSq = radiusNearNorm * radiusNearNorm;

		const std::int32_t y0 = std::max<std::int32_t>(0, (std::int32_t)(cy - rLm));
		const std::int32_t y1 = std::min(lmH - 1, (std::int32_t)(cy + rLm));
		const std::int32_t bx0 = std::max<std::int32_t>(0, (std::int32_t)(cx - rLm));
		const std::int32_t bx1 = std::min(lmW - 1, (std::int32_t)(cx + rLm));

		for (std::int32_t y = y0; y <= y1; y++) {
			const float dy = (y - cy) * invRLm;
			const float dySq = dy * dy;
			const float outerSq = 1.0f - dySq;
			if (outerSq <= 0.0f) {
				continue;
			}
			// Texels inside the disc on this row (the ceil/floor of the two real bounds)
			const float halfW = std::sqrt(outerSq) * rLm;
			const std::int32_t xs = std::max(bx0, (std::int32_t)std::ceil(cx - halfW));
			const std::int32_t xe = std::min(bx1, (std::int32_t)std::floor(cx + halfW));
			if (xs > xe) {
				continue;
			}
			float* row = &lightmap[((std::size_t)y * lmW + xs) * 2];

			// The flat core, if this row crosses it
			std::int32_t cs = xe + 1;
			std::int32_t ce = xe;
			if (nearSq > dySq) {
				const float halfCore = std::sqrt(nearSq - dySq) * rLm;
				cs = std::max(xs, (std::int32_t)std::ceil(cx - halfCore));
				ce = std::min(xe, (std::int32_t)std::floor(cx + halfCore));
				if (cs > ce) {
					cs = xe + 1;
					ce = xe;
				}
			}

			// Left annulus segment [xs, cs), the core [cs, ce], the right segment (ce, xe]
			std::int32_t x = xs;
#pragma GCC unroll 4
			for (; x < cs; x++, row += 2) {
				const float dx = ((float)x - cx) * invRLm;
				const float dist = Distance(dx * dx + dySq);
				const float t = std::min(std::max((1.0f - dist) * invDenom, 0.0f), 1.0f);
				const float strength = t * t * t;
				row[0] += strength * intensity;
				row[1] += strength * brightness;
			}
			for (; x <= ce; x++, row += 2) {
				row[0] += intensity;
				row[1] += brightness;
			}
#pragma GCC unroll 4
			for (; x <= xe; x++, row += 2) {
				const float dx = ((float)x - cx) * invRLm;
				const float dist = Distance(dx * dx + dySq);
				const float t = std::min(std::max((1.0f - dist) * invDenom, 0.0f), 1.0f);
				const float strength = t * t * t;
				row[0] += strength * intensity;
				row[1] += strength * brightness;
			}
		}
	}
}
