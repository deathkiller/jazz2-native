#pragma once

#include "../../Common.h"

namespace Jazz2::UI
{
	enum class Alignment
	{
		Center = 0x00,

		Left = 0x01,
		Right = 0x02,
		Top = 0x04,
		Bottom = 0x08,

		TopLeft = Top | Left,
		TopRight = Top | Right,
		BottomLeft = Bottom | Left,
		BottomRight = Bottom | Right,

		HorizontalMask = Left | Center | Right,
		VerticalMask = Top | Center | Bottom
	};

	DEFINE_ENUM_OPERATORS(Alignment);
}