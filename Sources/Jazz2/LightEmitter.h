#pragma once

#include "../nCine/Primitives/Vector2.h"

using namespace nCine;

namespace Jazz2
{
	enum class LightTypeType
	{
		Solid,
		WithNoise
	};
	
	struct LightEmitter
	{
		Vector2f Pos;
		float Intensity;
		float Brightness;
		float RadiusNear;
		float RadiusFar;
		LightTypeType Type;
	};
}