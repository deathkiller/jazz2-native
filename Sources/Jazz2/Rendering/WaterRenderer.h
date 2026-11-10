#pragma once

#include "../../Main.h"
#include "../../nCine/Graphics/SceneNode.h"

using namespace nCine;

namespace Jazz2
{
	class LevelHandler;
}

namespace Jazz2::Rendering
{
	/**
		@brief Draws the water in front of a layer behind the others

		Node of the level scene, visited by the scene viewport of every player. It hands the water to the viewport that is
		visiting it, which draws it at its depth in the middle of the scene when it's in front of a layer that isn't the
		frontmost visible one, `jjWaterLayer` of JJ2+ (see @ref PlayerViewport::DrawWater()). Otherwise the combine pass
		draws the water in front of everything and this does nothing.
	*/
	class WaterRenderer : public SceneNode
	{
	public:
		/** @brief Creates a new instance for a given level */
		WaterRenderer(LevelHandler* levelHandler);

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		LevelHandler* _levelHandler;
	};
}
