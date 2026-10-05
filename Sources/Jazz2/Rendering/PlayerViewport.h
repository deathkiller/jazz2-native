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

		std::unique_ptr<CombineRenderer> _combineRenderer;

#if defined(RHI_CAP_POSTPROCESSING)
		// Lighting render target + bloom blur chain. Only the full post-processing tier (shaders AND
		// framebuffers, see RhiFwd.h) renders lighting into an off-screen buffer and blurs it; the direct
		// tier renders the scene straight to the screen buffer and composites the CPU lightmap instead
		std::unique_ptr<LightingRenderer> _lightingRenderer;
		std::unique_ptr<Viewport> _lightingView;
		std::unique_ptr<Texture> _lightingBuffer;
		BlurRenderPass _downsamplePass;
		BlurRenderPass _blurPass2;
		BlurRenderPass _blurPass1;
		BlurRenderPass _blurPass3;
		BlurRenderPass _blurPass4;
#endif

		std::unique_ptr<Viewport> _view;
		std::unique_ptr<Texture> _viewTexture;	// Scene render target for the full tier; unused (null) on the direct tier
		std::unique_ptr<Camera> _camera;

		Rectf _viewBounds;
		Vector2f _cameraPos;
		Vector2f _cameraLastPos;
		Vector2f _cameraDistanceFactor;
		// Vertical follow anchor for the deadzone (see UpdateCamera): the camera holds this Y while the player makes
		// only small vertical movements, so bumps on uneven ground don't jolt the view.
		float _cameraViewCenterY;
		// Direction (-1 or 1) the lead of a small view points in, and how long a standing player has been facing
		// the other way (see UpdateCamera)
		float _lookAheadDirectionX;
		float _lookAheadTurnTime;
		// How far the reforged camera has moved off its look-ahead onto the target itself, from 0 to 1, while the weapon
		// wheel is held open (see UpdateCamera)
		float _centerOnTargetProgress;
		// Where the view settles once it's fully on the target, relative to where it is now (zero once it's there, and
		// with the original's camera, which never moves onto it). HUD::DrawWeaponWheel() draws the wheel there, so it
		// opens around the player and rides the view to the middle instead of the player sliding in under it.
		Vector2f _centerOnTargetOffset;
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
#if defined(RHI_CAP_POSTPROCESSING)
		/** @brief Returns the most blurred level of the blur chain, or `nullptr` if blur effects are disabled */
		Texture* GetBlurredTarget() const;
#endif
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
		/** @brief Places the look-ahead where it rests for the target actor as it stands now, instead of easing into it */
		void ResetLookAhead();

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
		// A view at most half the default width (LevelHandler::DefaultWidth - the Nintendo 64's 320x240, a player's
		// quarter of a splitscreen) shows so little of what is ahead that a lead proportional to the speed leaves the
		// player standing centred with nothing to see and running into what the view has not shown yet. So a small
		// view always leads, in the direction the player faces, and further while moving (see IsSmallView()).
		//
		// The lead held while standing, as a fraction of the half-view - 40 px of a 320 px view.
		static constexpr float SmallViewIdleLeadFraction = 0.25f;
		// Cap of the whole lead in a small view, the resting lead plus the usual speed lead, in place of
		// MaxLookAheadFraction - 96 px of a 320 px view, which still leaves the player 64 px from the edge behind it.
		static constexpr float SmallViewMaxLookAheadFraction = 0.6f;
		// How long (in frames at 60 FPS) a standing player has to face the other way before the lead follows, so
		// turning round to shoot does not swing the view back and forth. Moving turns it at once.
		static constexpr float SmallViewTurnDelay = 30.0f;
		// How long (in frames at 60 FPS) the view takes to move off the look-ahead onto the player while the weapon
		// wheel is held open, so the wheel ends up in the middle of the view, and how long it takes to go back once
		// the wheel is released. Both are eased in and out; the wheel moves along (see _centerOnTargetOffset).
		static constexpr float CenterOnTargetDuration = 16.0f;
		static constexpr float CenterOnTargetReturnDuration = 30.0f;
		// Vertical deadzone: the camera holds its Y while the player stays within +-this many pixels of it, so small
		// bumps (steps, slopes, landing jitter) don't move the view; it snaps to follow once the player leaves the band.
		static constexpr float VerticalDeadzone = 24.0f;
		// Within the deadzone the camera recenters toward the player at this per-frame rate - slow, so brief bumps are
		// ignored, but it doesn't stay offset after a jump. Freezes once within VerticalRecenterThreshold px to avoid a
		// pixel-by-pixel crawl.
		static constexpr float VerticalRecenter = 0.05f;
		static constexpr float VerticalRecenterThreshold = 4.0f;

		// --- The original's camera, used when PreferencesCache::EnableReforgedCamera is off ---
		// Measured off the original tick by tick; see the camera section of Docs/MovementAccuracyReference.dox.
		// How far the lead is aimed is a property of the player (Player::GetCameraLookAhead); these two are how
		// it gets there, and both are per one of the original's 70 Hz ticks.
		//
		// Fraction of the remaining gap closed each tick. Read straight off the end of the approach, where the
		// steps go 1.000, 0.500, 0.250, 0.125, 0.0625 - halving exactly.
		static constexpr float LegacyCameraApproach = 0.5f;
		// ...but never more than this per tick, which is what turns the first and longest part of the approach
		// into a straight 1 px/tick ramp. A velocity, so it scales with the frame rate where the fraction above
		// does not. This clamp is also active from the very first tick of a walk *and* of a dash, which is how
		// the target is known to be a distance rather than something proportional to the speed.
		static constexpr float LegacyCameraMaxStep = 1.0f;

		/** @brief Returns `true` if the view is at most half the default width, which makes the camera lead further */
		bool IsSmallView() const;
	};
}