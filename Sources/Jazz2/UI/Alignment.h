#pragma once

#include "../../Main.h"

namespace Jazz2::UI
{
	/**
		@brief Alignment
		
		Describes how content is positioned within its bounds by combining independent horizontal and
		vertical edges, with convenience corner values and masks for isolating each axis. Supports a
		bitwise combination of its member values.
	*/
	enum class Alignment
	{
		Center = 0x00,								/**< Center */

		Left = 0x01,								/**< Left */
		Right = 0x02,								/**< Right */
		Top = 0x04,									/**< Top */
		Bottom = 0x08,								/**< Bottom */

		TopLeft = Top | Left,						/**< Top-left */
		TopRight = Top | Right,						/**< TopRight */
		BottomLeft = Bottom | Left,					/**< Bottom-left */
		BottomRight = Bottom | Right,				/**< Bottom-right */

		HorizontalMask = Left | Center | Right,		/**< Mask of horizontal alignment */
		VerticalMask = Top | Center | Bottom		/**< Mask of vertical alignment */
	};

	DEATH_ENUM_FLAGS(Alignment);
}