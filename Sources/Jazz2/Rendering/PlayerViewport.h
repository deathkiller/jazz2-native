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

#if defined(RHI_CAP_SHADERS)
		// Bloom blur chain. Only backends with cheap shaders run the post-process bloom; the software backend
		// renders the scene straight to the screen buffer with no blur passes (see RhiFwd.h)
		BlurRenderPass _downsamplePass;
		BlurRenderPass _blurPass2;
		BlurRenderPass _blurPass1;
		BlurRenderPass _blurPass3;
		BlurRenderPass _blurPass4;
#endif

		std::unique_ptr<Viewport> _view;
		std::unique_ptr<Texture> _viewTexture;	// Scene render target for the shader path; unused (null) on the software backend
		std::unique_ptr<Camera> _camera;

		Rectf _viewBounds;
		Vector2f _cameraPos;
		Vector2f _cameraLastPos;
		Vector2f _cameraDistanceFactor;
		// Vertical follow anchor for the deadzone (see UpdateCamera): the camera holds this Y while the player makes
		// only small vertical movements, so bumps on uneven ground don't jolt the view.
		float _cameraViewCenterY;
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
		// Below this focus speed there is no look-ahead - the camera stays exactly on the player, so slow nudging or
		// pushing keeps the player perfectly centered and pixel-crisp. Above it the lead eases in, scaled by the excess.
		static constexpr float CameraStickSpeed = 1.0f;
		// Look-ahead distance per unit of (excess) player speed - how far ahead, in pixels, the camera leads at speed.
		// ~2x the old camera's lead (a full-speed walk now leads ~65px); raise for an even bigger lead.
		static constexpr float LookAheadFactorX = 22.0f;
		static constexpr float LookAheadFactorY = 14.0f;
		// The look-ahead is capped at this fraction of the half-view, so very high speeds (dashing/falling) can't shove
		// the player too far toward the screen edge.
		static constexpr float MaxLookAheadFraction = 0.4f;
		// Per-frame rate at which the look-ahead eases toward its target (cumulative + smooth - it never snaps in/out).
		static constexpr float LookAheadSmoothing = 0.04f;
		// Vertical deadzone: the camera holds its Y while the player stays within +-this many pixels of it, so small
		// bumps (steps, slopes, landing jitter) don't move the view; it snaps to follow once the player leaves the band.
		static constexpr float VerticalDeadzone = 24.0f;
		// Within the deadzone the camera recenters toward the player at this per-frame rate - slow, so brief bumps are
		// ignored, but it doesn't stay offset after a jump. Freezes once within VerticalRecenterThreshold px to avoid a
		// pixel-by-pixel crawl.
		static constexpr float VerticalRecenter = 0.05f;
		static constexpr float VerticalRecenterThreshold = 4.0f;
	};
}