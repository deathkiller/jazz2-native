#pragma once

#include "LevelHandler.h"

namespace Jazz2
{
	class PlayerViewport;

	class LightingRenderer : public SceneNode
	{
	public:
		LightingRenderer(PlayerViewport* owner)
			: _owner(owner), _renderCommandsCount(0)
		{
			_emittedLightsCache.reserve(32);
			setVisitOrderState(SceneNode::VisitOrderState::Disabled);
		}

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		PlayerViewport* _owner;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
		std::int32_t _renderCommandsCount;
		SmallVector<LightEmitter, 0> _emittedLightsCache;

		RenderCommand* RentRenderCommand();
	};

	class BlurRenderPass : public SceneNode
	{
	public:
		BlurRenderPass(PlayerViewport* owner)
			: _owner(owner)
		{
			setVisitOrderState(SceneNode::VisitOrderState::Disabled);
		}

		void Initialize(Texture* source, std::int32_t width, std::int32_t height, const Vector2f& direction);
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

	class CombineRenderer : public SceneNode
	{
	public:
		CombineRenderer(PlayerViewport* owner)
			: _owner(owner)
		{
			setVisitOrderState(SceneNode::VisitOrderState::Disabled);
		}

		void Initialize(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
		Rectf GetBounds() const;

		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		PlayerViewport* _owner;
		RenderCommand _renderCommand;
		RenderCommand _renderCommandWithWater;
		Rectf _bounds;
	};

	class PlayerViewport
	{
	public:
		LevelHandler* _levelHandler;
		Actors::Player* _targetPlayer;

		std::unique_ptr<LightingRenderer> _lightingRenderer;
		std::unique_ptr<CombineRenderer> _combineRenderer;
		std::unique_ptr<Viewport> _lightingView;
		std::unique_ptr<Texture> _lightingBuffer;

		BlurRenderPass _downsamplePass;
		BlurRenderPass _blurPass2;
		BlurRenderPass _blurPass1;
		BlurRenderPass _blurPass3;
		BlurRenderPass _blurPass4;

		std::unique_ptr<Viewport> _view;
		std::unique_ptr<Texture> _viewTexture;
		std::unique_ptr<Camera> _camera;

		Rectf _viewBounds;
		Vector2f _cameraPos;
		Vector2f _cameraLastPos;
		Vector2f _cameraDistanceFactor;
		Vector2f _cameraResponsiveness;
		float _shakeDuration;
		Vector2f _shakeOffset;
		float _ambientLightTarget;
		Vector4f _ambientLight;

		PlayerViewport(LevelHandler* levelHandler, Actors::Player* targetPlayer);

		bool Initialize(SceneNode* sceneNode, SceneNode* outputNode, Recti bounds, bool useHalfRes);
		void Register();

		Rectf GetBounds() const;
		Vector2i GetViewportSize() const;
		Actors::Player* GetTargetPlayer() const;
		void OnEndFrame();
		void UpdateCamera(float timeMult);
		void ShakeCameraView(float duration);
		void WarpCameraToTarget(bool fast);
	};
}