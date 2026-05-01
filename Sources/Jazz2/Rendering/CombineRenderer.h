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

#if !defined(RHI_CAP_SHADERS)
	/// Uniform data passed to the SW combine fragment shader
	struct CombineShaderData
	{
		float ambientR, ambientG, ambientB, ambientW;
		float invDarknessDiv;
		float waterLevelNorm;
		bool hasWater;
		float time;
		float camY;

		// Pre-computed lighting texture info
		const std::uint8_t* lightPixels;
		std::int32_t lightW, lightH;
		std::uint32_t lightScaleX_fp;	// 16.16 fixed-point scale: lightW / texW
		std::uint32_t lightScaleY_fp;	// 16.16 fixed-point scale: lightH / texH
		bool lightNeedsBilinear;		// true if lighting res differs from view res
	};
#endif

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
#if defined(RHI_CAP_SHADERS) && defined(RHI_CAP_FRAMEBUFFERS)
		RenderCommand _renderCommandWithWater;
#endif
		Rectf _bounds;

#if !defined(RHI_CAP_SHADERS)
		struct CombineShaderData _combineData;
#endif
	};
}