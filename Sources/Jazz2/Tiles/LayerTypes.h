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
		or color-tinted fill, one of the two layers that show something of what is behind them (x-ray and blur), or
		one of the special textured background effects (sky or circle) used for distant parallax backdrops.

		The x-ray and blur layers need a backend that can sample a render target in a shader. Elsewhere an x-ray
		layer is drawn as a default one showing its animated tiles as they are outside the objects and a blur layer
		isn't drawn at all, so a level looks as it would without the effects.
	*/
	enum class LayerRendererType {
		Default,				/**< Default rendering */
		Solid,					/**< Solid color rendering */
		Tinted,					/**< Color-tinted rendering */
		/**
			X-ray --- the last frame of an animated tile of the layer is its mask frame, shown only where an object is
			behind it, in the object's silhouette. Elsewhere the tile animates through the frames before it as usual,
			and a tile of a single frame shows nothing at all. Any frame may be the empty tile, which cuts the object
			out of the layer or shows the tile only over the object. Static tiles are drawn as usual.
		*/
		XRay,
		/**
			Blur behind --- the layer isn't drawn, it shows what is below it instead, per pixel by the channels of its
			tiles: moved horizontally by red and vertically by green (128 leaves it in place, every step away from it
			moves it by a quarter of the layer's pixel, so up to 32 pixels either way), mirrored vertically by blue,
			and blurred by alpha - the reflection too, so a gradient fades a reflection out into a blur. A pixel of
			no alpha does nothing at all, whatever its colour, so alpha 1 is one that moves or mirrors without a
			blur. The alpha of the layer's color sets how strong all of it is. The mirror line of a reflection is the
			first visible row of the topmost tile of the uninterrupted column of reflective tiles it belongs to, so a
			taller column reflects more of what is above it. The blur follows the blur effects option, the rest is
			always shown.
		*/
		BlurBehind,

		Sky = 10,				/**< Textured background --- Sky */
		Circle					/**< Textured background --- Circle */
	};
}
