#pragma once

#if defined(WITH_RHI_SOFTWARE)

#include <cstdint>

namespace nCine::RhiSoftware
{
	// CPU-dispatched scanline operations shared by the immediate rasterizer (SwRaster.cpp, which owns the
	// implementations and their SSE2/AVX2/NEON/SIMD128 variants) and the tile rasterizer (SwTileRasterizer.cpp).
	// Both hot loops must run the same best-for-this-CPU code path; keeping a per-TU copy caused the tile path -
	// the one that renders nearly every frame - to fall back to scalar on x86.

	/** @brief Blends a scanline of RGBA8 pixels over the destination with SrcAlpha/OneMinusSrcAlpha factors */
	void BlendScanlineSrcAlpha(std::uint8_t* dst, const std::uint8_t* src, std::int32_t count);

	/** @brief Multiplies a scanline of RGBA8 pixels in place by a constant color (values 0-256 per channel) */
	void TintScanline(std::uint8_t* buf, std::int32_t count, std::int32_t tR, std::int32_t tG, std::int32_t tB, std::int32_t tA);

	/**
	 * @brief Blends one constant RGBA8 source color over a scanline with SrcAlpha/OneMinusSrcAlpha factors
	 *
	 * The solid (no-texture) fill path: every destination pixel receives `(src * a + dst * (255 - a)) >> 8`
	 * per channel with the alpha channel using `(a * 255 + dst.a * (255 - a)) >> 8` - bit-identical to the
	 * per-pixel fast-blend the tile rasterizer runs, including the destination alpha. `src[3]` is expected
	 * in `(0, 255)`; the fully transparent / fully opaque shortcuts are the caller's (skip / plain fill).
	 */
	void BlendScanlineConstSrcAlpha(std::uint8_t* dst, std::int32_t count, const std::uint8_t src[4]);

	/**
	 * @brief Applies the dynamic-lighting combine to one row of screen pixels
	 *
	 * The per-row core of @ref SwDevice::ApplyPendingSoftwareLighting(): for each pixel `x` it samples the
	 * half-resolution lightmap row (`lmRow`, 2 floats per texel, indexed by `min(x / scale, lmW - 1)`),
	 * clamps both channels to `[0, 1]` and composites in place:
	 * `lit = main * (1 + g) + max(g - 0.7, 0)`, then `out = mix(lit, ambient, clamp(1 - r, 0, 1))`.
	 * A fully lit texel (`r >= 1`, `g <= 0`) leaves the pixel bytes untouched. The SIMD variants keep the
	 * scalar float operations in the same order, so the output is bit-identical to the scalar loop
	 * (fully lit lanes round-trip exactly; the alpha byte is rewritten with its own value).
	 */
	void CombineLightingScanline(std::uint8_t* px, std::int32_t width, const float* lmRow, std::int32_t lmW,
		std::int32_t scale, float ambR, float ambG, float ambB);
}

#endif
