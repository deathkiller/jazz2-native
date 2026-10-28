#include "PlayerViewport.h"
#include "../ContentResolver.h"
#include "../PreferencesCache.h"
#include "../Actors/Player.h"
#include "../UI/HUD.h"

#include "../../nCine/tracy.h"
#include "../../nCine/Base/Random.h"
#include "../../nCine/Graphics/RenderQueue.h"

namespace Jazz2::Rendering
{
	PlayerViewport::PlayerViewport(LevelHandler* levelHandler, Actors::ActorBase* targetActor)
		: _levelHandler(levelHandler), _targetActor(targetActor),
#if defined(RHI_CAP_POSTPROCESSING)
			_downsamplePass(this), _blurPass1(this), _blurPass2(this), _blurPass3(this), _blurPass4(this),
#endif
#if defined(TILEMAP_LAYER_EFFECTS)
			_behindCopyPass(this), _behindDownsamplePass(this), _behindBlurPass1(this), _behindBlurPass2(this),
#endif
			_cameraViewCenterY(0.0f), _lookAheadDirectionX(1.0f), _lookAheadTurnTime(0.0f), _centerOnTargetProgress(0.0f),
			_shakeDuration(0.0f), _cameraEaseProgress(1.0f), _cameraScale(1.0f), _freeCamera(false), _cameraFixedX(false), _cameraFixedY(false)
	{
		_ambientLight = levelHandler->_defaultAmbientLight;
		_ambientLightTarget = _ambientLight.W;
	}

	bool PlayerViewport::Initialize(SceneNode* sceneNode, SceneNode* outputNode, Recti bounds, bool useHalfRes)
	{
		bool notInitialized = (_view == nullptr);

		std::int32_t w = bounds.W;
		std::int32_t h = bounds.H;

		if (useHalfRes) {
			w *= 2;
			h *= 2;
		}

		if (notInitialized) {
#if !defined(RHI_CAP_POSTPROCESSING)
			// Direct tier (no shaders or no framebuffers): the scene is rasterized straight into the screen
			// buffer via a textureless viewport (no intermediate FBO); the combine and rescale shader passes
			// are skipped downstream. The viewport rect keeps the on-screen position so splitscreen players
			// land in their own region (on the shader tier the position comes from the CombineRenderer quad
			// instead), and the scissor clips sprites overhanging the region into a neighbouring player's view
			_view = std::make_unique<Viewport>();
			_view->SetViewportRect(bounds);
			_view->SetScissorRect(bounds);
			// The device clear of a screen pass covers the whole screen, which would wipe the other
			// players' already-drawn regions in splitscreen; the screen buffer is cleared once per frame
			// by the backend instead
			_view->SetClearMode(Viewport::ClearMode::Never);
#else
			_viewTexture = std::make_unique<Texture>(nullptr, Texture::ColorTargetFormat, w, h);
			_view = std::make_unique<Viewport>(_viewTexture.get(), Viewport::DepthStencilFormat::None);
#endif

			_camera = std::make_unique<Camera>();

			_view->SetCamera(_camera.get());
			_view->SetRootNode(sceneNode);
		} else {
#if !defined(RHI_CAP_POSTPROCESSING)
			_view->SetViewportRect(bounds);
			_view->SetScissorRect(bounds);
#else
			_view->RemoveAllTextures();
			_viewTexture->Init(nullptr, Texture::ColorTargetFormat, w, h);
			_view->SetTexture(_viewTexture.get());
#endif
		}

		// Y down like the screen - every pass, a render target's included, is top-down (see RhiFwd.h)
		_camera->SetOrthoProjection(0.0f, (float)w, 0.0f, (float)h);

#if defined(RHI_CAP_POSTPROCESSING)
		_viewTexture->SetMagFiltering(SamplerFilter::Nearest);
		_viewTexture->SetWrap(SamplerWrapping::ClampToEdge);

#if defined(RHI_GL_PROFILE_ES2) || defined(DEATH_TARGET_EMSCRIPTEN)
		// OpenGL|ES 2.0 cannot render into a two-channel texture (GL_RG8 is ES 3.0 and the profile's RG8
		// substitute LUMINANCE_ALPHA is not color-renderable), so the lighting buffer is a full RGBA target
		// there - the lighting shader writes all four channels and Combine only reads .r/.g either way.
		// WebGL 2.0 substitutes RG8 the same way and for the same reason (it has no texture swizzle, see
		// GLTextureFormat::Resolve()), so it inherits the substitution here as well
		constexpr Texture::Format LightingBufferFormat = Texture::Format::RGBA8;
#else
		constexpr Texture::Format LightingBufferFormat = Texture::Format::RG8;
#endif
		if (notInitialized) {
			_lightingRenderer = std::make_unique<LightingRenderer>(this);
			_lightingBuffer = std::make_unique<Texture>(nullptr, LightingBufferFormat,
				w * PreferencesCache::LightingResolutionPercent / 100, h * PreferencesCache::LightingResolutionPercent / 100);
			_lightingView = std::make_unique<Viewport>(_lightingBuffer.get(), Viewport::DepthStencilFormat::None);
			_lightingView->SetRootNode(_lightingRenderer.get());
			_lightingView->SetCamera(_camera.get());
		} else {
			_lightingView->RemoveAllTextures();
			_lightingBuffer->Init(nullptr, LightingBufferFormat,
				w * PreferencesCache::LightingResolutionPercent / 100, h * PreferencesCache::LightingResolutionPercent / 100);
			_lightingView->SetTexture(_lightingBuffer.get());
		}

		_lightingBuffer->SetMagFiltering(SamplerFilter::Nearest);
		_lightingBuffer->SetWrap(SamplerWrapping::ClampToEdge);

		if (PreferencesCache::BlurEffects) {
			// The blur targets are sized to the displayed (logical) viewport size rather than the (possibly
			// supersampled) texture size, so the blur strength - and the in-game bloom - stay consistent in splitscreen
			// zoom-out. Otherwise the larger per-player texture makes the blur cover a smaller fraction of the view
			//
			// The half-size level is what a lighting buffer at 50% already is, and below full lighting resolution the
			// chain is shortened, because every pass is a whole off-screen scene - and on the PS Vita a scene the
			// backend waits out before the next one may sample it, so the pass count costs more than their pixels:
			//  - Below 100% the first blur pass reads the view directly instead of a downsampled copy of it, its
			//    bilinear taps averaging the texels they land between. That covers one halving, and one is all that
			//    is needed - except in zoom-out, where the view texture is twice the size and has to be halved twice.
			//  - At 25% and below, where the lighting buffer is no finer than the quarter-size level itself, that
			//    level is dropped as well and the half-size one stands in for it (see CombineRenderer::OnDraw()).
			const bool downsampleFirst = (useHalfRes || PreferencesCache::LightingResolutionPercent >= 100);
			const bool quarterLevel = (PreferencesCache::LightingResolutionPercent > 25);
			if (downsampleFirst) {
				_downsamplePass.Initialize(_viewTexture.get(), w / 2, h / 2, Vector2f(0.0f, 0.0f));
			} else {
				_downsamplePass.Dispose();
			}
			_blurPass1.Initialize(downsampleFirst ? _downsamplePass.GetTarget() : _viewTexture.get(),
				bounds.W / 2, bounds.H / 2, Vector2f(1.0f, 0.0f));
			_blurPass2.Initialize(_blurPass1.GetTarget(), bounds.W / 2, bounds.H / 2, Vector2f(0.0f, 1.0f));
			if (quarterLevel) {
				_blurPass3.Initialize(_blurPass2.GetTarget(), bounds.W / 4, bounds.H / 4, Vector2f(1.0f, 0.0f));
				_blurPass4.Initialize(_blurPass3.GetTarget(), bounds.W / 4, bounds.H / 4, Vector2f(0.0f, 1.0f));
			} else {
				_blurPass3.Dispose();
				_blurPass4.Dispose();
			}
		} else {
			_downsamplePass.Dispose();
			_blurPass1.Dispose();
			_blurPass2.Dispose();
			_blurPass3.Dispose();
			_blurPass4.Dispose();
		}
#endif
#if defined(TILEMAP_LAYER_EFFECTS)
		InitializeLayerEffects(w, h);
#endif
		// On the direct tier no lighting render target exists at all: lighting is composited on the CPU by
		// CombineRenderer (the former inert _lightingView stub is gone)

		if (notInitialized) {
			_combineRenderer = std::make_unique<CombineRenderer>(this);
			_combineRenderer->setParent(outputNode);
		}

		_combineRenderer->Initialize(bounds.X, bounds.Y, bounds.W, bounds.H);

		return notInitialized;
	}

	void PlayerViewport::Register()
	{
#if defined(RHI_CAP_POSTPROCESSING)
		if (PreferencesCache::BlurEffects) {
			// A pass the chain was shortened by (see Initialize()) is disposed and registers nothing
			_blurPass4.Register();
			_blurPass3.Register();
			_blurPass2.Register();
			_blurPass1.Register();
			_downsamplePass.Register();
		}
#endif

#if defined(TILEMAP_LAYER_EFFECTS)
		// The passes that blur what is behind the blur layers are drawn by the scene view itself, in the middle of its
		// queue, so they are only in the chain to be visited and committed with the rest
		_view->RemoveSplitPasses();
		_layerEffects.CopyPasses = 0;
		_layerEffects.BlurPasses = 0;
		if (_layerEffects.BehindBlur != nullptr || _layerEffects.BehindCopy != nullptr) {
			for (BlurRenderPass* pass : { &_behindCopyPass, &_behindDownsamplePass, &_behindBlurPass1, &_behindBlurPass2 }) {
				if (pass->GetView() != nullptr) {
					pass->Register();
					std::uint32_t bit = _view->AddSplitPass(pass->GetView());
					// A layer asks only for the passes of what it samples
					(pass == &_behindCopyPass ? _layerEffects.CopyPasses : _layerEffects.BlurPasses) |= bit;
				}
			}
		}
#endif

		auto& chain = Viewport::GetChain();
#if !defined(RHI_CAP_POSTPROCESSING)
		// Direct tier: only the scene view exists (no lighting render target; the CPU lightmap composite
		// runs inside the draw phase instead), rendering straight into the screen buffer
		chain.push_back(_view.get());
#else
		chain.push_back(_lightingView.get());
		chain.push_back(_view.get());
#endif
#if defined(TILEMAP_LAYER_EFFECTS)
		// Registered after the scene, so it's drawn before it - the x-ray layers sample it
		if (_xrayMaskView != nullptr) {
			chain.push_back(_xrayMaskView.get());
		}
#endif
	}

	Rectf PlayerViewport::GetBounds() const
	{
		// Scripts ask already while the level is loaded, before the viewport is initialized
		return (_combineRenderer != nullptr ? _combineRenderer->GetBounds() : Rectf());
	}

	Vector2i PlayerViewport::GetViewportSize() const
	{
#if !defined(RHI_CAP_POSTPROCESSING)
		Recti viewportRect = _view->GetViewportRect();
		return Vector2i(viewportRect.W, viewportRect.H);
#else
		return _viewTexture->GetSize();
#endif
	}

#if defined(RHI_CAP_POSTPROCESSING)
	Texture* PlayerViewport::GetBlurredTarget() const
	{
		// The quarter-size level does not exist at a low lighting resolution (see Initialize()), and the half-size
		// one takes its place. With blur effects disabled neither exists.
		Texture* blurQuarter = _blurPass4.GetTarget();
		return (blurQuarter != nullptr ? blurQuarter : _blurPass2.GetTarget());
	}
#endif

#if defined(TILEMAP_LAYER_EFFECTS)
	void PlayerViewport::InitializeLayerEffects(std::int32_t width, std::int32_t height)
	{
		// Only what the level asks for, so a level without the special layers has no extra pass at all
		Tiles::TileMap* tileMap = _levelHandler->_tileMap.get();
		bool needsMask = (tileMap != nullptr && tileMap->HasXRayLayers());
		const bool hasBlurLayers = (tileMap != nullptr && tileMap->HasBlurBehindLayers());
		// Blurring what is behind a layer follows the blur effects option, a reflection or displacement of it is always
		// shown - a blur layer with neither isn't drawn at all. A reflection layer mirrors the same sharp copy, and a chroma
		// key layer compares its colors with the key.
		bool needsBehindBlur = (hasBlurLayers && PreferencesCache::BlurEffects);
		// What is behind the sprite layer is dithered as one image from such a copy too
		bool needsBehindCopy = ((hasBlurLayers && tileMap->BlurBehindLayersNeedSharpCopy()) ||
			(tileMap != nullptr && (tileMap->HasReflectionLayers() || tileMap->HasChromaKeyLayers())) ||
			(tileMap != nullptr && PreferencesCache::Dithering == DitheringScope::BehindSprites));
		if ((needsMask || needsBehindBlur || needsBehindCopy) && !ContentResolver::Get().CompileLayerEffectShaders()) {
			LOGW("Shaders of the special tile layers are not available, the layers are drawn without their effects");
			needsMask = false;
			needsBehindBlur = false;
			needsBehindCopy = false;
		}

		_layerEffects.View = _view.get();
		_layerEffects.XRayMask = nullptr;
		_layerEffects.XRayDepth = 0;

		if (needsMask) {
			// The format of the lighting buffer, which every backend of this tier is known to render into (OpenGL|ES 2.0
			// cannot render into fewer than four channels). Only the red channel is read.
#	if defined(RHI_GL_PROFILE_ES2) || defined(DEATH_TARGET_EMSCRIPTEN)
			constexpr Texture::Format MaskFormat = Texture::Format::RGBA8;
#	else
			constexpr Texture::Format MaskFormat = Texture::Format::RG8;
#	endif
			// The size of the scene target, so a fragment of the scene finds its own texel in the mask
			if (_xrayMaskView == nullptr) {
				_xrayMaskRenderer = std::make_unique<XRayMaskRenderer>(this);
				_xrayMaskBuffer = std::make_unique<Texture>(nullptr, MaskFormat, width, height);
				_xrayMaskView = std::make_unique<Viewport>(_xrayMaskBuffer.get(), Viewport::DepthStencilFormat::None);
				_xrayMaskView->SetRootNode(_xrayMaskRenderer.get());
				_xrayMaskView->SetClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			} else {
				_xrayMaskView->RemoveAllTextures();
				_xrayMaskBuffer->Init(nullptr, MaskFormat, width, height);
				_xrayMaskView->SetTexture(_xrayMaskBuffer.get());
			}
			_xrayMaskView->SetCamera(_camera.get());
			_xrayMaskBuffer->SetMagFiltering(SamplerFilter::Nearest);
			_xrayMaskBuffer->SetMinFiltering(SamplerFilter::Nearest);
			_xrayMaskBuffer->SetWrap(SamplerWrapping::ClampToEdge);
		} else {
			_xrayMaskView = nullptr;
			_xrayMaskBuffer = nullptr;
			_xrayMaskRenderer = nullptr;
		}

		if (needsBehindBlur) {
			// A blur of its own rather than the bloom chain: that one is rendered from the whole scene, this one only
			// from what is below the layer. Every step is at most a halving, as in the bloom chain (see Initialize()).
#	if defined(RHI_LOW_POWER_GPU)
			// Where every pass is a whole scene of its own (see Initialize()), the first blur reads the view directly
			// and the chain stays at half size
			_behindDownsamplePass.Dispose();
			_behindBlurPass1.Initialize(_viewTexture.get(), width / 2, height / 2, Vector2f(1.0f, 0.0f));
			_behindBlurPass2.Initialize(_behindBlurPass1.GetTarget(), width / 2, height / 2, Vector2f(0.0f, 1.0f));
#	else
			_behindDownsamplePass.Initialize(_viewTexture.get(), width / 2, height / 2, Vector2f(0.0f, 0.0f));
			_behindBlurPass1.Initialize(_behindDownsamplePass.GetTarget(), width / 4, height / 4, Vector2f(1.0f, 0.0f));
			_behindBlurPass2.Initialize(_behindBlurPass1.GetTarget(), width / 4, height / 4, Vector2f(0.0f, 1.0f));
#	endif
			_layerEffects.BehindBlur = _behindBlurPass2.GetTarget();
		} else {
			_behindDownsamplePass.Dispose();
			_behindBlurPass1.Dispose();
			_behindBlurPass2.Dispose();
			_layerEffects.BehindBlur = nullptr;
		}

		if (needsBehindCopy) {
			// A sharp copy of what is behind the layer for the reflections to mirror and the displacement to move, taken
			// first in the middle of the scene (see Register()). A plain copy rather than a downsampling pass of the same
			// size, which would blur it.
#	if defined(RHI_LOW_POWER_GPU)
			// Half the pixels to write and read where every pass costs a whole scene
			_behindCopyPass.InitializeCopy(_viewTexture.get(), width / 2, height / 2);
#	else
			_behindCopyPass.InitializeCopy(_viewTexture.get(), width, height);
#	endif
			_layerEffects.BehindCopy = _behindCopyPass.GetTarget();
		} else {
			_behindCopyPass.Dispose();
			_layerEffects.BehindCopy = nullptr;
		}

		LOGD("Special tile layers: x-ray mask {}, behind blur {}, behind copy {}", needsMask ? "allocated" : "not needed",
			needsBehindBlur ? "allocated" : "not needed", needsBehindCopy ? "allocated" : "not needed");
	}
#endif

	Actors::ActorBase* PlayerViewport::GetTargetActor() const
	{
		return _targetActor;
	}

	void PlayerViewport::OnEndFrame()
	{
#if defined(RHI_CAP_POSTPROCESSING)
		_lightingView->SetClearColor(_ambientLight.W, 0.0f, 0.0f, 1.0f);
#endif
	}

	void PlayerViewport::UpdateCamera(float timeMult)
	{
		ZoneScopedC(0x4876AF);

		// Ambient Light Transition
		if (_ambientLight.W != _ambientLightTarget) {
			float step = timeMult * 0.012f;
			if (std::abs(_ambientLight.W - _ambientLightTarget) < step) {
				_ambientLight.W = _ambientLightTarget;
			} else {
				_ambientLight.W += step * ((_ambientLightTarget < _ambientLight.W) ? -1.0f : 1.0f);
			}
		}

		// Viewport bounds animation
		if (_viewBounds != _levelHandler->_viewBoundsTarget) {
			if (std::abs(_viewBounds.X - _levelHandler->_viewBoundsTarget.X) < 2.0f) {
				_viewBounds = _levelHandler->_viewBoundsTarget;
			} else {
				constexpr float TransitionSpeed = 0.02f;
				float dx = (_levelHandler->_viewBoundsTarget.X - _viewBounds.X) * TransitionSpeed * timeMult;
				_viewBounds.X += dx;
				_viewBounds.W -= dx;
			}
		}

		if (_freeCamera) {
			// Stays where it's put, at its own scale - screen = (world - center) * scale + half of the view
			Vector2f viewOrigin = _cameraPos * _cameraScale - GetViewportSize().As<float>() * 0.5f;
			if (!PreferencesCache::UnalignedViewport) {
				viewOrigin = Vector2f(floorFast(viewOrigin.X), floorFast(viewOrigin.Y));
			}
			_camera->SetView(viewOrigin, 0.0f, _cameraScale);
			return;
		}
		if (_targetActor == nullptr) {
			return;
		}

		// The position to focus on
		Vector2i halfView = GetViewportSize() / 2;
		Vector2f focusPos = _targetActor->GetPos();

		// Smooth, cumulative look-ahead. The camera base is locked to the player (see the clamp below), so the camera
		// never lags behind - this is purely a lead in the direction of travel. The lead eases toward a target that is
		// proportional to how much the player's speed exceeds CameraStickSpeed (so it builds up and decays gradually),
		// and is applied rounded to whole pixels (clamp below) so the player never shimmers - only clean 1px steps.
		// A player that has speed but didn't actually move (pushing into a wall) gets no lead.
		Vector2f focusSpeed = _targetActor->GetSpeed();
		Vector2f actualMovement = focusPos - _cameraLastPos;
		_cameraLastPos = focusPos;
		if (std::abs(actualMovement.X) < 0.01f) {
			focusSpeed.X = 0.0f;
		}
		if (std::abs(actualMovement.Y) < 0.01f) {
			focusSpeed.Y = 0.0f;
		}

		// How far the view is moved off the look-ahead and the vertical anchor onto the player itself, see below
		float centerOnTarget = 0.0f;

		if (PreferencesCache::EnableReforgedCamera) {
			Vector2f focusVelocity = Vector2f(std::abs(focusSpeed.X), std::abs(focusSpeed.Y));
			const bool movingX = (focusVelocity.X > CameraStickSpeed);
			const float speedLookAheadX = (movingX ? (focusVelocity.X - CameraStickSpeed) * LookAheadFactorX : 0.0f);
			float maxLookAheadY = halfView.Y * MaxLookAheadFraction;
			float targetLookAheadX;
			if (IsSmallView()) {
				// A small view always leads: by a resting amount in the direction the player faces, and by the usual
				// speed lead on top of that while moving. Moving points it at once; a player standing still has to
				// keep facing the other way for a moment before it follows, so turning round to shoot does not swing
				// the view (see SmallViewIdleLeadFraction)
				if (movingX) {
					_lookAheadDirectionX = (focusSpeed.X < 0.0f ? -1.0f : 1.0f);
					_lookAheadTurnTime = 0.0f;
				} else if ((_targetActor->IsFacingLeft() ? -1.0f : 1.0f) != _lookAheadDirectionX) {
					_lookAheadTurnTime += timeMult;
					if (_lookAheadTurnTime >= SmallViewTurnDelay) {
						_lookAheadDirectionX = -_lookAheadDirectionX;
						_lookAheadTurnTime = 0.0f;
					}
				} else {
					_lookAheadTurnTime = 0.0f;
				}
				targetLookAheadX = _lookAheadDirectionX * std::min(halfView.X * SmallViewIdleLeadFraction + speedLookAheadX,
					halfView.X * SmallViewMaxLookAheadFraction);
			} else {
				targetLookAheadX = (movingX ? (focusSpeed.X < 0.0f ? -1.0f : 1.0f) * std::min(speedLookAheadX, halfView.X * MaxLookAheadFraction) : 0.0f);
			}
			float targetLookAheadY = (focusVelocity.Y > CameraStickSpeed
				? (focusSpeed.Y < 0.0f ? -1.0f : 1.0f) * std::min((focusVelocity.Y - CameraStickSpeed) * LookAheadFactorY, maxLookAheadY) : 0.0f);
			// Ease the look-ahead toward its target, but once the player has stopped and the lead is within a few pixels
			// of where it rests (zero, or the resting lead of a small view), freeze it instead of crawling the rest of
			// the way: the camera is pixel-snapped, so easing through the last few pixels shows up as visible 1px jumps.
			// Holding the small difference avoids that; easing resumes the moment the player moves again.
			static constexpr float LookAheadFreezeThreshold = 4.0f;
			if (movingX || std::abs(targetLookAheadX - _cameraDistanceFactor.X) >= LookAheadFreezeThreshold) {
				_cameraDistanceFactor.X = lerpByTime(_cameraDistanceFactor.X, targetLookAheadX, LookAheadSmoothing, timeMult);
			}
			if (targetLookAheadY != 0.0f || std::abs(_cameraDistanceFactor.Y) >= LookAheadFreezeThreshold) {
				_cameraDistanceFactor.Y = lerpByTime(_cameraDistanceFactor.Y, targetLookAheadY, LookAheadSmoothing, timeMult);
			}

			// Vertical deadzone: hold the camera's vertical anchor while the player stays within +-VerticalDeadzone of it,
			// so small bumps on uneven ground (steps, slopes, landing jitter) don't make the view bob. Snap to follow once
			// the player leaves the band, and recenter slowly within it (freezing when nearly centered, to avoid a 4px
			// crawl) so the camera doesn't stay offset after a jump. The anchor stays whole-pixel and is floored downstream,
			// so the player itself stays crisp - this only desensitizes the vertical follow, it doesn't smear it.
			float verticalOffset = focusPos.Y - _cameraViewCenterY;
			if (verticalOffset > VerticalDeadzone) {
				_cameraViewCenterY = focusPos.Y - VerticalDeadzone;
			} else if (verticalOffset < -VerticalDeadzone) {
				_cameraViewCenterY = focusPos.Y + VerticalDeadzone;
			} else if (std::abs(verticalOffset) >= VerticalRecenterThreshold) {
				_cameraViewCenterY = lerpByTime(_cameraViewCenterY, focusPos.Y, VerticalRecenter, timeMult);
			}

			// The weapon wheel ends up in the middle of the view (see HUD::DrawWeaponWheel()), which is not where the
			// player is while the view leads, so the view moves onto the player while the wheel is held open, and back
			// once it's released. Both the look-ahead and the anchor keep being tracked underneath, so it goes back to
			// where they are by then, not to where they were when the wheel was opened.
			bool wheelOpen = false;
			if (auto* player = runtime_cast<Actors::Player>(_targetActor)) {
				wheelOpen = (_levelHandler->_hud != nullptr && _levelHandler->_hud->IsWeaponWheelOpen(player->GetPlayerIndex()));
			}
			if (wheelOpen) {
				_centerOnTargetProgress = std::min(_centerOnTargetProgress + timeMult / CenterOnTargetDuration, 1.0f);
			} else if (_centerOnTargetProgress > 0.0f) {
				_centerOnTargetProgress = std::max(_centerOnTargetProgress - timeMult / CenterOnTargetReturnDuration, 0.0f);
			}
			// Eased in and out (smoothstep), so the view neither lurches off nor stops dead
			centerOnTarget = _centerOnTargetProgress * _centerOnTargetProgress * (3.0f - 2.0f * _centerOnTargetProgress);
		} else {
			// The original's camera, measured tick by tick. Three things differ from the above and each one is a
			// separate complaint about how this engine pans:
			//
			// - The target is aimed by the direction being *held*, not by the speed being carried. That is what
			//   holds the view still through a sidekick - 16 px/tick with nothing pressed pans the original not at
			//   all, where reading the speed runs the lead out to 117 px here.
			// - It is a flat distance per movement state (28 px walking, 120 dashing) rather than something
			//   proportional to the speed. Reforged leads 80 px at a walk, nearly three times the original.
			// - It closes half the gap per tick but never by more than a pixel, so almost the whole approach is a
			//   straight 1 px/tick ramp that stops dead on arrival - not an exponential ease that is still
			//   creeping after 140 frames.
			float targetLookAheadX = 0.0f;
			float targetLookAheadY = 0.0f;
			if (auto* player = runtime_cast<Actors::Player>(_targetActor)) {
				targetLookAheadX = player->GetCameraLookAhead();
				targetLookAheadY = player->GetCameraVerticalOffset();
			}
			// The constants are per one of the original's 70 Hz ticks, so the elapsed frame is converted into
			// ticks once and both the fraction and the clamp are applied in that unit
			float ticks = timeMult * Actors::Player::LegacyFrameRateScale;
			float wanted = lerpByTime(_cameraDistanceFactor.X, targetLookAheadX, LegacyCameraApproach, ticks);
			float maxStep = LegacyCameraMaxStep * ticks;
			_cameraDistanceFactor.X += std::clamp(wanted - _cameraDistanceFactor.X, -maxStep, maxStep);

			// No vertical deadzone, and no vertical lead except during a buttstomp. Measured, the original's
			// camera otherwise holds the player's own Y to within one tick of its vertical movement - +7.75 px
			// at the launch of a 132 px jump, back to zero by the apex - so there is nothing to reproduce but
			// the player's position. The deadzone above is what leaves this engine holding a stale 21 px offset
			// into the next scenario, which is the opposite of fixed.
			//
			// The stomp is the exception, and it uses the same approach at twice the step - see
			// @ref Actors::Player::LegacyCameraButtstompDrop.
			float wantedY = lerpByTime(_cameraDistanceFactor.Y, targetLookAheadY, LegacyCameraApproach, ticks);
			float maxStepY = LegacyCameraMaxStep * 2.0f * ticks;
			_cameraDistanceFactor.Y += std::clamp(wantedY - _cameraDistanceFactor.Y, -maxStepY, maxStepY);
			_cameraViewCenterY = focusPos.Y;
		}

		if (_shakeDuration > 0.0f) {
			_shakeDuration -= timeMult;

			if (_shakeDuration <= 0.0f) {
				_shakeOffset = Vector2f::Zero;
			} else {
				float shakeFactor = 0.1f * timeMult;
				_shakeOffset.X = lerp(_shakeOffset.X, nCine::Random().NextFloat(-0.2f, 0.2f) * halfView.X, shakeFactor) * std::min(_shakeDuration * 0.1f, 1.0f);
				_shakeOffset.Y = lerp(_shakeOffset.Y, nCine::Random().NextFloat(-0.2f, 0.2f) * halfView.Y, shakeFactor) * std::min(_shakeDuration * 0.1f, 1.0f);
			}
		}

		// The camera base is the player's position plus the look-ahead rounded to a whole pixel: a constant integer
		// offset between the floored player sprite and the floored camera is what keeps the player pixel-crisp (it does
		// clean 1px steps as the lead grows/shrinks, never sub-pixel shimmer). Then clamp to the level bounds.
		// The offset to where the view settles once it's on the player is taken the same way, without the shake (the HUD
		// doesn't shake) and aligned like the view, so the weapon wheel moves in the same whole-pixel steps as the level
		Vector2f lookAhead = _cameraDistanceFactor * (1.0f - centerOnTarget);
		float viewCenterY = lerp(_cameraViewCenterY, focusPos.Y, centerOnTarget);
		const auto alignView = [](float pos) { return (PreferencesCache::UnalignedViewport ? pos : floorFast(pos)); };
		_centerOnTargetOffset = Vector2f::Zero;
		if (_viewBounds.W > halfView.X * 2) {
			const float minX = _viewBounds.X + halfView.X, maxX = _viewBounds.X + _viewBounds.W - halfView.X;
			const float cameraX = std::clamp(focusPos.X + roundFast(lookAhead.X), minX, maxX);
			_cameraPos.X = alignView(cameraX + _shakeOffset.X);
			_centerOnTargetOffset.X = alignView(std::clamp(focusPos.X, minX, maxX)) - alignView(cameraX);
		} else {
			_cameraPos.X = floorFast(_viewBounds.X + _viewBounds.W * 0.5f + _shakeOffset.X);
		}
		if (_viewBounds.H > halfView.Y * 2) {
			const float minY = _viewBounds.Y + halfView.Y - 1.0f, maxY = _viewBounds.Y + _viewBounds.H - halfView.Y - 2.0f;
			// The vertical anchor is off the player by whole pixels too - floored on its own while it eases back onto the
			// player, it stepped in other frames than the player did, which shook by a pixel on a moving platform
			const float cameraY = std::clamp(focusPos.Y + roundFast(viewCenterY - focusPos.Y + lookAhead.Y), minY, maxY);
			_cameraPos.Y = alignView(cameraY + _shakeOffset.Y);
			_centerOnTargetOffset.Y = alignView(std::clamp(focusPos.Y, minY, maxY)) - alignView(cameraY);
		} else {
			_cameraPos.Y = floorFast(_viewBounds.Y + _viewBounds.H * 0.5f + _shakeOffset.Y);
		}
		if (!PreferencesCache::EnableReforgedCamera) {
			// The original's camera doesn't move onto the player for the wheel, so the wheel stays in the middle
			_centerOnTargetOffset = Vector2f::Zero;
		}

		// A script may fix the camera on an axis (cameraFreeze() of JJ2+), not kept in the level there, and the view doesn't
		// move onto the player for the wheel either. It's followed underneath all the same, so letting go of the camera
		// eases back to where it would be by then.
		if (_cameraFixedX) {
			_cameraPos.X = alignView(_cameraFixedPos.X + _shakeOffset.X);
			_centerOnTargetOffset.X = 0.0f;
		}
		if (_cameraFixedY) {
			_cameraPos.Y = alignView(_cameraFixedPos.Y + _shakeOffset.Y);
			_centerOnTargetOffset.Y = 0.0f;
		}
		// Getting there, or back onto the player, eased in and out, unless it was instant
		if (_cameraEaseProgress < 1.0f) {
			_cameraEaseProgress = std::min(_cameraEaseProgress + timeMult / CameraFixEaseDuration, 1.0f);
			float t = _cameraEaseProgress * _cameraEaseProgress * (3.0f - 2.0f * _cameraEaseProgress);
			_cameraPos = Vector2f(alignView(lerp(_cameraEaseFrom.X, _cameraPos.X, t)), alignView(lerp(_cameraEaseFrom.Y, _cameraPos.Y, t)));
		}

		_camera->SetView(_cameraPos - halfView.As<float>(), 0.0f, 1.0f);
	}

	void PlayerViewport::ShakeCameraView(float duration)
	{
		if (_shakeDuration < duration) {
			_shakeDuration = duration;
		}
	}

	void PlayerViewport::OverrideCamera(CameraOverride modeX, float x, CameraOverride modeY, float y, bool centered, bool instant)
	{
		// Kept as the center of the view. Fixed where it already is, it isn't moved again, which a script that fixes it each
		// frame relies on to get there; anything else eases from where the camera is now, so both axes can share the easing.
		Vector2i halfView = GetViewportSize() / 2;
		Vector2f center = (centered ? Vector2f(x, y) : Vector2f(x + float(halfView.X), y + float(halfView.Y)));
		bool changed = false;
		if (modeX != CameraOverride::Keep && !(modeX == CameraOverride::Fix && _cameraFixedX && _cameraFixedPos.X == center.X)) {
			_cameraFixedX = (modeX == CameraOverride::Fix);
			_cameraFixedPos.X = center.X;
			changed = true;
		}
		if (modeY != CameraOverride::Keep && !(modeY == CameraOverride::Fix && _cameraFixedY && _cameraFixedPos.Y == center.Y)) {
			_cameraFixedY = (modeY == CameraOverride::Fix);
			_cameraFixedPos.Y = center.Y;
			changed = true;
		}
		if (changed) {
			_cameraEaseFrom = _cameraPos;
			_cameraEaseProgress = (instant ? 1.0f : 0.0f);
		}
	}

	void PlayerViewport::WarpCameraToTarget(bool fast)
	{
		if (_freeCamera || _targetActor == nullptr) {
			return;
		}

		// The camera base is locked to the player, so warping just means placing it on the player and resetting the
		// movement-tracking reference. A non-fast (hard) warp also resets the look-ahead, so it starts where it rests.
		Vector2f focusPos = _targetActor->GetPos();
		_cameraPos = focusPos;
		_cameraLastPos = focusPos;
		_cameraViewCenterY = focusPos.Y;
		// Nothing to ease from, a fixed camera is put where it's fixed by the next update
		_cameraEaseProgress = 1.0f;
		if (!fast) {
			ResetLookAhead();
		}
	}

	void PlayerViewport::ResetLookAhead()
	{
		if (_targetActor == nullptr) {
			_cameraDistanceFactor = Vector2f::Zero;
			return;
		}

		// Centred, except in a small view of the reforged camera, which rests on a lead in the direction the player
		// faces - set straight away, so a level start or a respawn does not open with the view drifting by itself
		_lookAheadDirectionX = (_targetActor->IsFacingLeft() ? -1.0f : 1.0f);
		_lookAheadTurnTime = 0.0f;
		const bool leads = (PreferencesCache::EnableReforgedCamera && IsSmallView());
		_cameraDistanceFactor = Vector2f(leads ? _lookAheadDirectionX * (GetViewportSize().X / 2) * SmallViewIdleLeadFraction : 0.0f, 0.0f);
	}

	void PlayerViewport::SetFreeCamera(Vector2f center, float scale)
	{
		_freeCamera = true;
		_cameraPos = center;
		_cameraLastPos = center;
		_cameraScale = (scale > 0.0f ? scale : 1.0f);
		_shakeDuration = 0.0f;
		_shakeOffset = Vector2f::Zero;
	}

	void PlayerViewport::SetFollowCamera()
	{
		_freeCamera = false;
		_cameraScale = 1.0f;
		WarpCameraToTarget(false);
	}

	Vector2f PlayerViewport::GetWorldViewSize() const
	{
		return GetViewportSize().As<float>() / _cameraScale;
	}

	Vector2f PlayerViewport::WorldToView(Vector2f worldPos) const
	{
		return (worldPos - _cameraPos) * _cameraScale + GetViewportSize().As<float>() * 0.5f;
	}

	Vector2f PlayerViewport::ViewToWorld(Vector2f viewPos) const
	{
		return (viewPos - GetViewportSize().As<float>() * 0.5f) / _cameraScale + _cameraPos;
	}

	bool PlayerViewport::IsSmallView() const
	{
		// Only the width: it is what the horizontal lead has to fit in, and the one that shrinks in a narrow view
		return (GetViewportSize().X <= LevelHandler::DefaultWidth / 2);
	}
}