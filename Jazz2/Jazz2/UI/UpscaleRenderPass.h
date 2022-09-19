#pragma once

#include "../ContentResolver.h"

#include "../../nCine/Graphics/RenderCommand.h"
#include "../../nCine/Graphics/SceneNode.h"
#include "../../nCine/Graphics/Camera.h"
#include "../../nCine/Graphics/Viewport.h"

using namespace nCine;

namespace Jazz2::UI
{
	class UpscaleRenderPass : public SceneNode
	{
	public:
		UpscaleRenderPass()
			:
			_resizeShader(nullptr)
		{
			setVisitOrderState(SceneNode::VisitOrderState::DISABLED);
		}

		void Initialize(int width, int height, int targetWidth, int targetHeight);
		void Register();

		bool OnDraw(RenderQueue& renderQueue) override;

		SceneNode* GetNode() const {
			return _node.get();
		}

		Vector2i GetViewSize() {
			return _view->size();
		}

	private:
		std::unique_ptr<SceneNode> _node;
		std::unique_ptr<Texture> _target;
		std::unique_ptr<Viewport> _view;
		std::unique_ptr<Camera> _camera;
#if defined(ALLOW_RESCALE_SHADERS)
		Shader* _resizeShader;
#endif
		RenderCommand _renderCommand;
		Vector2f _targetSize;
	};
}