#pragma once

#include "../../nCine/Base/Algorithms.h"

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
#if defined(DEATH_TARGET_N64)
		// `sqrt.s` is ~58 unpipelined cycles on the VR4300 and this runs once per lit texel; a table over
		// the [0, 1] range the callers stay in (they clamp the result) is a convert and a load. 1024 steps
		// put the error under 1/2048 of the radius, far below what the bilinear stretch of the map shows.
		static float table[1026];
		static bool tableReady = false;
		if (!tableReady) {
			for (std::int32_t i = 0; i < 1026; i++) {
				table[i] = std::sqrt(float(i) / 1024.0f);
			}
			tableReady = true;
		}
		const std::int32_t index = std::min<std::int32_t>(std::max<std::int32_t>(std::int32_t(d2 * 1024.0f), 0), 1025);
		return table[index];
#elif defined(DEATH_TARGET_DREAMCAST)
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
	 * @brief Clamps a falloff parameter into [0, 1]
	 *
	 * On the PS2 through `fmax()`/`fmin()`, which GCC turns into the EE's own `MAX.S`/`MIN.S`: written as comparisons
	 * the clamp is two compares and two branches per texel there, each compare followed by a stall slot before its
	 * branch, and the branches keep the annulus loops below from being unrolled as intended. Elsewhere `fmin()` is the
	 * slower spelling (an extra NaN test without fast math), so the comparisons stay. The two agree on every input
	 * except NaN, which this argument never is.
	 */
	inline float ClampFalloff(float t)
	{
#if defined(DEATH_TARGET_PS2)
		return std::fmin(std::fmax(t, 0.0f), 1.0f);
#else
		return std::min(std::max(t, 0.0f), 1.0f);
#endif
	}

	/** @brief The falloff of `LightingFs.inc` at distance @p dist (in units of the far radius), see @ref SplatLight() */
	inline float FalloffStrength(float dist, float invDenom)
	{
		const float t = ClampFalloff((1.0f - dist) * invDenom);
		return t * t * t;
	}

#if defined(DEATH_TARGET_N64)
	/**
	 * @brief Returns the falloff of a light with no flat core over the squared-distance steps of @ref Distance()
	 *
	 * Most lights of a level have no core (a near radius of zero) - every collectible's glow, the lights of the
	 * enemies and bosses - and for those the falloff is a function of the squared distance alone, the same for
	 * every light. Tabulated over the very steps the square root is, it is a convert and a load per texel of the
	 * annulus instead of that and a subtraction, two compares with their branches and two multiplies. Every entry
	 * is computed from the square-root table's own value by the function the loop calls, so the lightmap comes out
	 * bit-identical.
	 */
	inline const float* CorelessFalloffTable()
	{
		static float table[1026];
		static bool tableReady = false;
		if (!tableReady) {
			for (std::int32_t i = 0; i < 1026; i++) {
				table[i] = FalloffStrength(Distance(float(i) / 1024.0f), 1.0f);
			}
			tableReady = true;
		}
		return table;
	}
#endif

	/** @brief The row loop of @ref SplatLight(), for a light with a flat core or (if @p Coreless) without one */
	template<bool Coreless>
	inline void SplatLightRows(float* lightmap, std::int32_t lmW, std::int32_t lmH, float cx, float cy, float rLm,
		float radiusNearNorm, float intensity, float brightness);

	/**
	 * @brief Returns @f$ \sqrt{v} @f$ for the half-width of a light's row, a positive value
	 *
	 * Twice a row, for the extents of the disc and of its flat core. Where the platform has a square root
	 * instruction that is what it uses; the Gekko/Broadway have none, and `std::sqrt` there is newlib's
	 * bit-by-bit software routine - two calls a row were a large part of a frame at a spot with a lot of
	 * lights. @ref Distance() is just as good for this: its few parts in a million can move a bound only for a
	 * texel that lies on it to within that much, where the strength is 0 (the disc's edge) or 1 (the core's
	 * edge, which the falloff meets at exactly 1) either way.
	 */
	inline float RowHalfWidth(float v)
	{
#if defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
		return Distance(v);
#else
		return std::sqrt(v);
#endif
	}

	/**
	 * @brief Adds one light to a two-channel (intensity, brightness) float lightmap
	 *
	 * @param lightmap        `lmW * lmH` texels of two floats each, row-major
	 * @param lmW             Lightmap width in texels
	 * @param lmH             Lightmap height in texels
	 * @param cx, cy          Light centre in lightmap texels
	 * @param rLm             Far radius in lightmap texels (at least 0.5)
	 * @param radiusNearNorm  Near radius as a fraction of the far radius (may exceed 1: no falloff)
	 * @param intensity       Added to the first channel of every texel the light reaches, scaled by the falloff
	 * @param brightness      Added to the second channel the same way
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
	 * can interleave several texels for the in-order cores this runs on; `dx` is recomputed from the texel's
	 * position rather than accumulated, so no iteration depends on the previous one beyond that position, which
	 * steps by one as a float - a whole number is exact in a float, so it is the very value converting `x` would
	 * give, and the conversion is a round trip through memory on PowerPC, which has no move between its
	 * integer and floating-point registers.
	 */
	inline void SplatLight(float* lightmap, std::int32_t lmW, std::int32_t lmH, float cx, float cy, float rLm,
		float radiusNearNorm, float intensity, float brightness)
	{
#if defined(DEATH_TARGET_N64)
		if (radiusNearNorm == 0.0f) {
			SplatLightRows<true>(lightmap, lmW, lmH, cx, cy, rLm, radiusNearNorm, intensity, brightness);
			return;
		}
#endif
		SplatLightRows<false>(lightmap, lmW, lmH, cx, cy, rLm, radiusNearNorm, intensity, brightness);
	}

	template<bool Coreless>
	inline void SplatLightRows(float* lightmap, std::int32_t lmW, std::int32_t lmH, float cx, float cy, float rLm,
		float radiusNearNorm, float intensity, float brightness)
	{
		const float invRLm = 1.0f / rLm;
#if defined(DEATH_TARGET_N64)
		const float* corelessFalloff = (Coreless ? CorelessFalloffTable() : nullptr);
		const float invRLmScaled = invRLm * 32.0f;
#endif
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
			// Texels inside the disc on this row (the ceil/floor of the two real bounds). The fast forms, because
			// `std::ceil` and `std::floor` are library calls on most of the consoles this runs on, four a row.
			const float halfW = RowHalfWidth(outerSq) * rLm;
			const std::int32_t xs = std::max(bx0, (std::int32_t)nCine::ceilFast(cx - halfW));
			const std::int32_t xe = std::min(bx1, (std::int32_t)nCine::floorFast(cx + halfW));
			if (xs > xe) {
				continue;
			}
			float* row = &lightmap[((std::size_t)y * lmW + xs) * 2];

#if defined(DEATH_TARGET_N64)
			if constexpr (Coreless) {
				// No core, so the whole row is annulus, and its squared distance only ever indexes the falloff table -
				// Distance()'s 1024 steps per unit - so the scale is folded into the two terms instead: (32 dx)^2 +
				// 1024 dy^2 is the very float (dx^2 + dy^2) * 1024 is, as scaling by a power of two commutes with the
				// rounding, for one multiplication a texel fewer. The texel's coordinate counts up as a float (exact
				// at any lightmap size) instead of being converted from the integer every time.
				const float dySqScaled = dySq * 1024.0f;
				float fx = float(xs);
				for (std::int32_t x = xs; x <= xe; x++, row += 2, fx += 1.0f) {
					const float dxScaled = (fx - cx) * invRLmScaled;
					const float strength = corelessFalloff[std::min<std::int32_t>(std::int32_t(dxScaled * dxScaled + dySqScaled), 1025)];
					row[0] += strength * intensity;
					row[1] += strength * brightness;
				}
				continue;
			}
#endif

			// The flat core, if this row crosses it
			std::int32_t cs = xe + 1;
			std::int32_t ce = xe;
			if (nearSq > dySq) {
				const float halfCore = RowHalfWidth(nearSq - dySq) * rLm;
				cs = std::max(xs, (std::int32_t)nCine::ceilFast(cx - halfCore));
				ce = std::min(xe, (std::int32_t)nCine::floorFast(cx + halfCore));
				if (cs > ce) {
					cs = xe + 1;
					ce = xe;
				}
			}

			// Left annulus segment [xs, cs), the core [cs, ce], the right segment (ce, xe]
			std::int32_t x = xs;
			float fx = (float)x;
#pragma GCC unroll 4
			for (; x < cs; x++, row += 2, fx += 1.0f) {
				const float dx = (fx - cx) * invRLm;
				const float strength = FalloffStrength(Distance(dx * dx + dySq), invDenom);
				row[0] += strength * intensity;
				row[1] += strength * brightness;
			}
			for (; x <= ce; x++, row += 2) {
				row[0] += intensity;
				row[1] += brightness;
			}
			fx = (float)x;
#pragma GCC unroll 4
			for (; x <= xe; x++, row += 2, fx += 1.0f) {
				const float dx = (fx - cx) * invRLm;
				const float strength = FalloffStrength(Distance(dx * dx + dySq), invDenom);
				row[0] += strength * intensity;
				row[1] += strength * brightness;
			}
		}
	}
}
