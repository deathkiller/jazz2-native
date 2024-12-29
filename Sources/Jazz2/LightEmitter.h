#pragma once

#include "../nCine/Primitives/Vector2.h"

using namespace nCine;

namespace Jazz2
{
	/** @brief Light emitter */
	struct LightEmitter
	{
		Vector2f Pos;
		float Intensity;
		float Brightness;
		float RadiusNear;
		float RadiusFar;
	};
}