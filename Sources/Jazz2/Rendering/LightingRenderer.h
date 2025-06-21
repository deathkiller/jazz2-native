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

	/** @brief Processes all lights in a scene into an intermediate target */
	class LightingRenderer : public SceneNode
	{
	public:
		LightingRenderer(PlayerViewport* owner);

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		PlayerViewport* _owner;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
		std::int32_t _renderCommandsCount;
		SmallVector<LightEmitter, 0> _emittedLightsCache;

		RenderCommand* RentRenderCommand();
	};
}