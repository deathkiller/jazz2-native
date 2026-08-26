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

	/** @brief Rec.601 luminance of an ambient colour, for a lightmap store with no colour channels */
	DEATH_ALWAYS_INLINE float AmbientLuminance(float r, float g, float b)
	{
		return 0.299f * r + 0.587f * g + 0.114f * b;
	}
}
