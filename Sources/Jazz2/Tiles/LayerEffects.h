#pragma once

#include "../../Main.h"
#include "../../nCine/Graphics/RHI/RhiFwd.h"

/*
	Whether the special tile layers - LayerRendererType::XRay and LayerRendererType::BlurBehind - are drawn with
	their effects. Both sample a render target in the fragment stage at the pixel being drawn, which only the full
	post-processing tier can (see RhiFwd.h), and both are built on the single-draw layer mesh. Everywhere else none
	of this is compiled: the layers are reduced when the level is loaded instead (see TileMap::LoadLayer()),
	an x-ray layer to a plain layer showing its animated tiles without their mask frames, a blur layer to a hidden one.
*/
#if defined(RHI_CAP_POSTPROCESSING) && defined(TILEMAP_USE_SINGLE_DRAW)
#	define TILEMAP_LAYER_EFFECTS
#endif

namespace nCine
{
	class Texture;
	class Viewport;
}

namespace Jazz2::Tiles
{
#if defined(TILEMAP_LAYER_EFFECTS) || defined(DOXYGEN_GENERATING_OUTPUT)
	/**
		@brief What the special tile layers need from the viewport they are drawn in

		Owned by the viewport that renders the level (one per player in splitscreen) and handed to the tile map
		through @ref ITileMapOwner::GetLayerEffectsTarget() while the scene is visited. The viewport fills in the
		textures, the tile map reports back what it drew.
	*/
	struct LayerEffectsTarget
	{
		/** @brief Scene viewport, split at the lowest blur-behind layer it draws (see @ref nCine::Viewport::RequestSplit()) */
		nCine::Viewport* View = nullptr;
		/** @brief Coverage of the actors behind the x-ray layers in the current frame, or `nullptr` if it wasn't rendered */
		nCine::Texture* XRayMask = nullptr;
		/** @brief Blurred scene behind the blur-behind layers, or `nullptr` if there is none (blur effects are off) */
		nCine::Texture* BehindBlur = nullptr;
		/** @brief Sharp copy of the scene behind the blur-behind layers, which they reflect and displace, or `nullptr` if none of them does */
		nCine::Texture* BehindCopy = nullptr;
		/**
			@brief Depth of the frontmost x-ray layer drawn, `0` if none

			Written by the tile map while the scene is visited, and read back by the mask pass of the next frame,
			which runs before the scene: the mask is only rendered while an x-ray layer is on screen, and only the
			actors behind that layer go into it.
		*/
		std::uint16_t XRayDepth = 0;
	};
#endif
}
