#include "WaterRenderer.h"
#include "PlayerViewport.h"

#include "../../nCine/Graphics/RenderResources.h"

namespace Jazz2::Rendering
{
	WaterRenderer::WaterRenderer(LevelHandler* levelHandler)
		: _levelHandler(levelHandler)
	{
		SetVisitOrderState(SceneNode::VisitOrderState::Disabled);
	}

	bool WaterRenderer::OnDraw(RenderQueue& renderQueue)
	{
		const Viewport* view = RenderResources::GetCurrentViewport();
		for (auto& viewport : _levelHandler->_assignedViewports) {
			if (viewport->_view.get() == view) {
				viewport->DrawWater(renderQueue);
				break;
			}
		}
		return true;
	}
}
