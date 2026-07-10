#pragma once

#include "../../Main.h"
#include "../LightEmitter.h"

#include "../../nCine/Graphics/RenderCommand.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/SceneNode.h"

#include <memory>

using namespace nCine;

namespace Jazz2::Rendering
{
	class PlayerViewport;

	/**
		@brief Processes all lights in a scene into an intermediate target
		
		Collects the lights emitted by every actor in the level and renders each as an additive blended quad
		into a @ref PlayerViewport's lighting buffer, which the @ref CombineRenderer later applies to the scene.
	*/
	class LightingRenderer : public SceneNode
	{
	public:
		/** @brief Creates a new instance attached to a given viewport */
		LightingRenderer(PlayerViewport* owner);

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		// Render command with its per-light uniform caches resolved once at creation
		struct LightCommand
		{
			std::unique_ptr<RenderCommand> Command;
			GLUniformCache* TexRectUniform;
			GLUniformCache* SpriteSizeUniform;
			GLUniformCache* ColorUniform;
		};

		PlayerViewport* _owner;
		SmallVector<LightCommand, 0> _renderCommands;
		std::int32_t _renderCommandsCount;
		SmallVector<LightEmitter, 0> _emittedLightsCache;

		LightCommand& RentRenderCommand();
	};
}