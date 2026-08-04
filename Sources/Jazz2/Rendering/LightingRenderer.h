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

		Collects the lights emitted by every actor in the level and renders them as additive blended quads
		into a @ref PlayerViewport's lighting buffer, which the @ref CombineRenderer later applies to the scene.
	*/
	class LightingRenderer : public SceneNode
	{
	public:
		/** @brief Creates a new instance attached to a given viewport */
		LightingRenderer(PlayerViewport* owner);

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		PlayerViewport* _owner;
		SmallVector<LightEmitter, 0> _emittedLightsCache;
#if defined(RHI_CAP_SHADERS) && defined(RHI_CAP_FRAMEBUFFERS)
		// Only the shader render path renders lights into a buffer; backends without cheap shaders skip lighting
		// entirely (see RhiFwd.h). Every visible light of the viewport is accumulated into one vertex stream and
		// submitted as a single draw - lights need no sorting and no texture, so unlike a tile layer they never
		// have to be grouped, only split when the shared array buffer cannot hold them all at once.
		SmallVector<float, 0> _vertices;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;

		void AppendLightQuad(const LightEmitter& light);
		RenderCommand* RentRenderCommand(std::int32_t index);
#endif
	};
}
