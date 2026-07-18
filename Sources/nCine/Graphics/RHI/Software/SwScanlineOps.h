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
}

#endif
