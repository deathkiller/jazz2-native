#pragma once

#include "../LevelHandler.h"

namespace Jazz2::Rendering
{
	class PlayerViewport;
	
	/** @brief Applies blur to a scene */
	class BlurRenderPass : public SceneNode
	{
	public:
		BlurRenderPass(PlayerViewport* owner)
			: _owner(owner)
		{
			setVisitOrderState(SceneNode::VisitOrderState::Disabled);
		}

		void Initialize(Texture* source, std::int32_t width, std::int32_t height, Vector2f direction);
		void Register();

		bool OnDraw(RenderQueue& renderQueue) override;

		Texture* GetTarget() const {
			return _target.get();
		}

	private:
		PlayerViewport* _owner;
		std::unique_ptr<Texture> _target;
		std::unique_ptr<Viewport> _view;
		std::unique_ptr<Camera> _camera;
		RenderCommand _renderCommand;

		Texture* _source;
		bool _downsampleOnly;
		Vector2f _direction;
	};
}