#pragma once

#include "../../Main.h"

#include "../../nCine/Graphics/RHI/RhiFwd.h"
#include "../../nCine/Graphics/RenderCommand.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/SceneNode.h"
#include "../../nCine/Primitives/Rect.h"

#if !defined(RHI_CAP_SHADERS) || !defined(RHI_CAP_FRAMEBUFFERS)
#	include "../LightEmitter.h"
#	include <vector>
#endif

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

#if !defined(RHI_CAP_SHADERS) || !defined(RHI_CAP_FRAMEBUFFERS)
		// Software renderer only: the shader lighting + combine passes are capability-gated out, so a cheap
		// CPU approximation of the dynamic lighting is applied straight to the screen buffer instead. Reused
		// across frames to avoid per-frame allocations. See PrepareSoftwareLighting(). The lightmap must stay
		// alive after the Visit phase (the software device reads it during the later Draw phase), so it is a
		// per-viewport member rather than a stack buffer.
		SmallVector<LightEmitter, 0> _swLightsCache;
		// Half-resolution accumulation buffer, 2 floats/texel: R=intensity, G=brightness
		SmallVector<float, 0> _swLightmap;

		/**
		 * @brief Builds the half-resolution dynamic lightmap on the CPU and hands it plus the water parameters to the software device (software backend)
		 *
		 * Runs in the Visit (queue-building) phase: collects the light emitters, splats them into @ref _swLightmap and
		 * submits the map plus this viewport's rectangle, ambient colour and water parameters (waterline, wave time,
		 * camera Y) to the device (SetPendingSoftwareLighting). The device applies the actual in-place combine -
		 * dynamic lighting and the lightweight per-row water effect that replaces the CombineWithWater shader
		 * variants - during the Draw phase, after the scene has been rasterized into the screen buffer and before
		 * the HUD, triggered by the Combine command @ref OnDraw queues. Returns `true` when a combine command should
		 * be queued, `false` when the scene is fully lit with no lights and no water is in view (when water is in
		 * view the combine is queued even fully lit, with no lightmap).
		 */
		bool PrepareSoftwareLighting();
#endif
	};
}