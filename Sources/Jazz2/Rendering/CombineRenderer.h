#pragma once

#include "../LevelHandler.h"

namespace Jazz2::Rendering
{
	class PlayerViewport;

	/** @brief Combines all previous passes of a scene into a resulting image */
	class CombineRenderer : public SceneNode
	{
	public:
		CombineRenderer(PlayerViewport* owner);

		void Initialize(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
		Rectf GetBounds() const;

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		PlayerViewport* _owner;
		RenderCommand _renderCommand;
		RenderCommand _renderCommandWithWater;
		Rectf _bounds;
	};
}