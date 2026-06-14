#pragma once

#include "../../Main.h"

#include "../../nCine/Graphics/RenderCommand.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/SceneNode.h"
#include "../../nCine/Primitives/Rect.h"

using namespace nCine;

namespace Jazz2::Rendering
{
	class PlayerViewport;

	/**
		@brief Combines all previous passes of a scene into a resulting image
		
		Final compositing pass of a @ref PlayerViewport that blends the rendered scene with the lighting buffer
		and the blur targets, applying ambient light and an optional underwater shader variant, and emits the
		result into the viewport's output region.
	*/
	class CombineRenderer : public SceneNode
	{
	public:
		/** @brief Creates a new instance attached to a given viewport */
		CombineRenderer(PlayerViewport* owner);

		/**
		 * @brief Initializes the renderer to a given region
		 *
		 * @param x       Horizontal position of the region
		 * @param y       Vertical position of the region
		 * @param width   Width of the region
		 * @param height  Height of the region
		 */
		void Initialize(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
		/** @brief Returns bounds of the combined region */
		Rectf GetBounds() const;

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		PlayerViewport* _owner;
		RenderCommand _renderCommand;
		RenderCommand _renderCommandWithWater;
		Rectf _bounds;
	};
}