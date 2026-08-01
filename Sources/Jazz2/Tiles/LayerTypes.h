#pragma once

#include "../../Main.h"

namespace Jazz2::Tiles
{
	/**
		@brief Describes how a layer's parallax scrolling speed is interpreted
		
		Selected per axis in @ref LayerDescription to control how a layer follows the camera --- the default parallax
		model, locking to the top/left of the screen, fitting the whole layer into view, or treating the speed values
		as multipliers of the camera size rather than its position.
	*/
	enum class LayerSpeedModel {
		/** @brief Default model */
		Default,			
		/** @brief Ignores all speed and offset settings to be tied to the top/left side of the screen */
		AlwaysOnTop,		
		/** @brief Ignores the speed and auto speed properties, and instead ensures that the full extent of this layer will be visible and no blank space outside of it will be shown */
		FitLevel,
		/** @brief Treats the layer's speed and auto speed properties on this axis as multipliers of the current camera size, rather than camera position */
		SpeedMultipliers
	};

	/**
		@brief Specifies how a layer is rendered
		
		Stored in @ref LayerDescription to choose the rendering path for a layer --- ordinary textured tiles, a solid
		or color-tinted fill, or one of the special textured background effects (sky or circle) used for distant
		parallax backdrops.
	*/
	enum class LayerRendererType {
		Default,				/**< Default rendering */
		Solid,					/**< Solid color rendering */
		Tinted,					/**< Color-tinted rendering */

		Sky = 10,				/**< Textured background --- Sky */
		Circle					/**< Textured background --- Circle */
	};
}
