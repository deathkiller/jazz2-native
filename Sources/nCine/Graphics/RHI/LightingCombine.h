#pragma once

#include <Common.h>

namespace nCine::RHI
{
	/*
		The CPU-lightmap combine shared by the fixed-function tiers.

		The shader path composites the lightmap in `Combine.shader`'s fragment stage. A backend with no
		fragment shaders cannot, so it converts the compositor's half-resolution float lightmap into a
		small texture of per-texel FACTORS and draws it over the viewport as one multiplicative quad -
		the multiply-only approximation of the shader's `mix(scene * (1 + g), ambient, 1 - r)`.

		The conversion itself is per-texel over the whole lightmap every frame, so it stays a hand-written
		loop in each backend: the store format (ARGB4444 in video memory on the PVR, tiled RGBA8 on the
		GX, IA16 on the RDP, an attenuation-only I8 on the GS), the quantization width and the run-length
		short-circuit are all backend business. What is NOT backend business is the FACTOR ITSELF, which
		is why it lives here: six copies of one formula had already drifted into three spellings.

		@ref LightingCombineFactor is the whole approximation. Everything else in a backend's loop is
		mechanism around it.
	*/

	/** @brief Clamps one raw lightmap channel into the [0, 1] the factor formula assumes */
	DEATH_ALWAYS_INLINE float ClampLightmapChannel(float v)
	{
		return (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
	}

	/**
		@brief The multiply-only lighting factor for ONE ambient channel

		`r` is the lightmap's coverage channel and `g` its brightness channel, both already clamped (see
		@ref ClampLightmapChannel); @p amb is the matching channel of the ambient colour - or a single
		grey/luma for a backend whose lightmap store has no colour (the GS and the RDP), which is the one
		place the tiers legitimately differ.

		Derived from the shader's `mix(main * (1 + light.g), ambient, 1 - light.r)`: a fully covered texel
		(`r = 1`) keeps the scene scaled by `1 + g` and takes no ambient, an uncovered one (`r = 0`) is
		pure ambient, and everything between interpolates. The result is NOT clamped - a caller whose
		store cannot hold values above 1 clamps as part of its own quantization.
	*/
	DEATH_ALWAYS_INLINE float LightingCombineFactor(float r, float g, float amb)
	{
		return r * (1.0f + g) + amb * (1.0f - r);
	}

	/**
		@brief The multiply-only lighting factors for all three ambient channels at once

		Per channel this is exactly @ref LightingCombineFactor(), but the two terms that do NOT depend on
		the ambient colour - the covered term `r * (1 + g)` and the uncovered weight `1 - r` - are
		computed once for the three of them instead of three times.

		That is worth a function rather than being left to the compiler, because the compiler does not do
		it. A backend's conversion loop runs this once per lightmap texel, and calling the single-channel
		form three times leaves the common subexpression to be spotted across three separate inline
		expansions - which GCC does not manage for the PSP's Allegrex. Measured on that target, the three
		separate calls compile to 24 `mul.s` and 24 `add.s`; this compiles to 7 and 7.
	*/
	DEATH_ALWAYS_INLINE void LightingCombineFactors(float r, float g, float ambR, float ambG, float ambB,
		float& outR, float& outG, float& outB)
	{
		const float covered = r * (1.0f + g);
		const float uncovered = 1.0f - r;
		outR = covered + ambR * uncovered;
		outG = covered + ambG * uncovered;
		outB = covered + ambB * uncovered;
	}

	/** @brief Rec.601 luminance of an ambient colour, for a lightmap store with no colour channels */
	DEATH_ALWAYS_INLINE float AmbientLuminance(float r, float g, float b)
	{
		return 0.299f * r + 0.587f * g + 0.114f * b;
	}
}
