#pragma once

#if defined(WITH_AMMX)

#include <cstdint>

// The AMMX (Apollo 68080) implementations of the two hottest scanline kernels, assembled from
// Sources/nCine/Backends/Amiga/AmigaAmmxOps.s by vasm (see the bit-exactness contract there).
// The binary itself stays 68060-safe: these are only ever called through the runtime gate below,
// which AmigaPlatform::Initialize() opens when the machine reports the 68080 attention flag
// (and the NCINE_NO_AMMX environment variable is not set).
extern "C" {
	void SwAmmxBlendScanlineSrcAlpha(std::uint8_t* dst, const std::uint8_t* src, std::int32_t count);
	void SwAmmxFusedLutBlendScanline(std::uint8_t* dst, const std::uint8_t* srcIdx, std::int32_t count, const std::uint8_t (*packed)[4]);
}

namespace nCine::RHI::Software
{
	/** @brief Routes @ref BlendScanlineSrcAlpha / @ref FusedLutBlendScanline through the AMMX kernels */
	void SetAmmxEnabled(bool enabled);
	bool IsAmmxEnabled();
}

#endif
