#pragma once

#include "../../Main.h"
#include "../Tiles/LayerEffects.h"

#if defined(TILEMAP_LAYER_EFFECTS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../nCine/Graphics/RenderCommand.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/SceneNode.h"

#include <memory>

using namespace nCine;

namespace Jazz2::Rendering
{
	class PlayerViewport;

	/**
		@brief Renders the coverage of the actors behind the x-ray tile layers

		The x-ray layers (see @ref Tiles::LayerRendererType::XRay) show a different frame of their tiles where an
		actor is behind them, which is decided per pixel by a mask: the alpha of every actor sprite below the
		frontmost x-ray layer, drawn into a @ref PlayerViewport's mask buffer before the scene itself. The mask is
		rendered only in a frame that follows one with an x-ray tile on screen, so a level that has none on screen
		pays nothing for it.
	*/
	class XRayMaskRenderer : public SceneNode
	{
	public:
		/** @brief Creates a new instance attached to a given viewport */
		XRayMaskRenderer(PlayerViewport* owner);

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		// The sprites of one texture, accumulated into one mesh
		struct Group
		{
			const Texture* DiffuseTexture;
			bool Indexed;
			std::int32_t VerticesIndex;
		};

		PlayerViewport* _owner;
		SmallVector<SmallVector<float, 0>, 0> _vertices;
		SmallVector<Group, 4> _groups;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;

		RenderCommand* RentRenderCommand(std::int32_t index, Shader* shader);
	};
}

#endif
