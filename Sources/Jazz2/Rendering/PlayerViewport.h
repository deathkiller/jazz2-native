#pragma once

#include "../LevelHandler.h"
#include "LightingRenderer.h"
#include "CombineRenderer.h"
#include "BlurRenderPass.h"

namespace Jazz2::Rendering
{
	/** @brief Player viewport */
	class PlayerViewport
	{
	public:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Hide these members from documentation before refactoring
		LevelHandler* _levelHandler;
		Actors::ActorBase* _targetActor;

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
#endif

		PlayerViewport(LevelHandler* levelHandler, Actors::ActorBase* targetActor);

		bool Initialize(SceneNode* sceneNode, SceneNode* outputNode, Recti bounds, bool useHalfRes);
		void Register();

		Rectf GetBounds() const;
		Vector2i GetViewportSize() const;
		Actors::ActorBase* GetTargetActor() const;
		void OnEndFrame();
		void UpdateCamera(float timeMult);
		void ShakeCameraView(float duration);
		void OverrideCamera(float x, float y, bool topLeft = false);
		void WarpCameraToTarget(bool fast);

	private:
		static constexpr float ResponsivenessChange = 0.04f;
		static constexpr float ResponsivenessMin = 0.3f;
		static constexpr float ResponsivenessMax = 1.2f;
		static constexpr float SlowRatioX = 0.3f;
		static constexpr float SlowRatioY = 0.3f;
		static constexpr float FastRatioX = 0.2f;
		static constexpr float FastRatioY = 0.04f;
	};
}