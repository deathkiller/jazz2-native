#pragma once

#include "../nCine/Primitives/Vector2.h"

using namespace nCine;

namespace Jazz2
{
	/** @brief Allows objects to emit light */
	struct LightEmitter
	{
		/** @brief Light position */
		Vector2f Pos;
		/** @brief Light intensity */
		float Intensity;
		/** @brief Light brightness */
		float Brightness;
		/** @brief Light near radius */
		float RadiusNear;
		/** @brief Light far radius */
		float RadiusFar;
	};
}