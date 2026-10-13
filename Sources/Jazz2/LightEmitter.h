#pragma once

#include "../nCine/Primitives/Vector2.h"

using namespace nCine;

namespace Jazz2
{
	/**
		@brief Allows objects to emit light

		Per-object light description (position, intensity, brightness and near/far radii) collected each frame and fed
		to the lighting renderer to illuminate the scene. A light with a non-zero @ref RingRadius is a ring instead of
		a point: its near/far radii are then measured from the ring's circle, so they describe the falloff across the
		ring's band on both sides of it.
	*/
	struct LightEmitter
	{
		/** @brief Light position (the centre of the ring for a ring light) */
		Vector2f Pos;
		/** @brief Light intensity */
		float Intensity;
		/** @brief Light brightness */
		float Brightness;
		/** @brief Light near radius */
		float RadiusNear;
		/** @brief Light far radius */
		float RadiusFar;
		/** @brief Radius of the ring the light is spread along, `0.0f` for a point light */
		float RingRadius = 0.0f;
	};
}
