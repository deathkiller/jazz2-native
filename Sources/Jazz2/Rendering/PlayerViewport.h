#pragma once

#include "../LevelHandler.h"
#include "LightingRenderer.h"
#include "CombineRenderer.h"
#include "BlurRenderPass.h"

namespace Jazz2::Rendering
{
	/**
		@brief Player viewport
		
		Owns the on-screen region rendered for a single player together with its full render pipeline: the
		scene viewport and camera that follows a target actor, the @ref LightingRenderer, the chain of
		@ref BlurRenderPass passes, and the @ref CombineRenderer. Also drives per-frame camera movement,
		shake, overrides and ambient light.
	*/
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

		/**
		 * @brief Creates a new instance
		 *
		 * @param levelHandler  Level handler that owns the viewport
		 * @param targetActor   Actor the camera follows
		 */
		PlayerViewport(LevelHandler* levelHandler, Actors::ActorBase* targetActor);

		/**
		 * @brief Initializes the viewport
		 *
		 * @param sceneNode   Root node of the rendered scene
		 * @param outputNode  Node the resulting image is attached to
		 * @param bounds      Bounds of the viewport
		 * @param useHalfRes  Whether to render at half resolution
		 */
		bool Initialize(SceneNode* sceneNode, SceneNode* outputNode, Recti bounds, bool useHalfRes);
		/** @brief Registers the viewport and its render passes into the viewport chain */
		void Register();

		/** @brief Returns bounds of the viewport */
		Rectf GetBounds() const;
		/** @brief Returns size of the viewport */
		Vector2i GetViewportSize() const;
		/** @brief Returns the actor the camera follows */
		Actors::ActorBase* GetTargetActor() const;
		/** @brief Called at the end of each frame */
		void OnEndFrame();
		/** @brief Updates the camera position */
		void UpdateCamera(float timeMult);
		/** @brief Shakes the camera view for a given duration */
		void ShakeCameraView(float duration);
		/** @brief Overrides the camera position */
		void OverrideCamera(float x, float y, bool topLeft = false);
		/** @brief Instantly moves the camera to the target actor */
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