#include "PlayerViewport.h"
#include "../PreferencesCache.h"
#include "../Actors/Player.h"

#include "../../nCine/tracy.h"
#include "../../nCine/Base/Random.h"
#include "../../nCine/Graphics/RenderQueue.h"

namespace Jazz2::Rendering
{
	PlayerViewport::PlayerViewport(LevelHandler* levelHandler, Actors::ActorBase* targetActor)
		: _levelHandler(levelHandler), _targetActor(targetActor),
			_downsamplePass(this), _blurPass1(this), _blurPass2(this), _blurPass3(this), _blurPass4(this),
			_cameraViewCenterY(0.0f), _shakeDuration(0.0f)
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
			_viewTexture = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, w, h);
			_view = std::make_unique<Viewport>(_viewTexture.get(), Viewport::DepthStencilFormat::None);

			_camera = std::make_unique<Camera>();

			_view->SetCamera(_camera.get());
			_view->SetRootNode(sceneNode);
		} else {
			_view->RemoveAllTextures();
			_viewTexture->Init(nullptr, Texture::Format::RGB8, w, h);
			_view->SetTexture(_viewTexture.get());
		}

		_viewTexture->SetMagFiltering(SamplerFilter::Nearest);
		_viewTexture->SetWrap(SamplerWrapping::ClampToEdge);

		_camera->SetOrthoProjection(0.0f, (float)w, (float)h, 0.0f);

		if (notInitialized) {
			_lightingRenderer = std::make_unique<LightingRenderer>(this);
			_lightingBuffer = std::make_unique<Texture>(nullptr, Texture::Format::RG8,
				w * PreferencesCache::LightingResolutionPercent / 100, h * PreferencesCache::LightingResolutionPercent / 100);
			_lightingView = std::make_unique<Viewport>(_lightingBuffer.get(), Viewport::DepthStencilFormat::None);
			_lightingView->SetRootNode(_lightingRenderer.get());
			_lightingView->SetCamera(_camera.get());
		} else {
			_lightingView->RemoveAllTextures();
			_lightingBuffer->Init(nullptr, Texture::Format::RG8,
				w * PreferencesCache::LightingResolutionPercent / 100, h * PreferencesCache::LightingResolutionPercent / 100);
			_lightingView->SetTexture(_lightingBuffer.get());
		}

		_lightingBuffer->SetMagFiltering(SamplerFilter::Nearest);
		_lightingBuffer->SetWrap(SamplerWrapping::ClampToEdge);

		if (PreferencesCache::BlurEffects) {
			_downsamplePass.Initialize(_viewTexture.get(), w / 2, h / 2, Vector2f(0.0f, 0.0f));
			_blurPass1.Initialize(_downsamplePass.GetTarget(), w / 2, h / 2, Vector2f(1.0f, 0.0f));
			_blurPass2.Initialize(_blurPass1.GetTarget(), w / 2, h / 2, Vector2f(0.0f, 1.0f));
			_blurPass3.Initialize(_blurPass2.GetTarget(), w / 4, h / 4, Vector2f(1.0f, 0.0f));
			_blurPass4.Initialize(_blurPass3.GetTarget(), w / 4, h / 4, Vector2f(0.0f, 1.0f));
		} else {
			_downsamplePass.Dispose();
			_blurPass1.Dispose();
			_blurPass2.Dispose();
			_blurPass3.Dispose();
			_blurPass4.Dispose();
		}

		if (notInitialized) {
			_combineRenderer = std::make_unique<CombineRenderer>(this);
			_combineRenderer->setParent(outputNode);
		}

		_combineRenderer->Initialize(bounds.X, bounds.Y, bounds.W, bounds.H);

		return notInitialized;
	}

	void PlayerViewport::Register()
	{
		if (PreferencesCache::BlurEffects) {
			_blurPass4.Register();
			_blurPass3.Register();
			_blurPass2.Register();
			_blurPass1.Register();
			_downsamplePass.Register();
		}

		auto& chain = Viewport::GetChain();
		chain.push_back(_lightingView.get());
		chain.push_back(_view.get());
	}

	Rectf PlayerViewport::GetBounds() const
	{
		return _combineRenderer->GetBounds();
	}

	Vector2i PlayerViewport::GetViewportSize() const
	{
		return _viewTexture->GetSize();
	}

	Actors::ActorBase* PlayerViewport::GetTargetActor() const
	{
		return _targetActor;
	}

	void PlayerViewport::OnEndFrame()
	{
		_lightingView->SetClearColor(_ambientLight.W, 0.0f, 0.0f, 1.0f);
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

		// The position to focus on
		Vector2i halfView = _view->GetSize() / 2;
		Vector2f focusPos = _targetActor->GetPos();

		bool overridePosX = false, overridePosY = false;
		// TODO: Not working correctly on some platforms
		/*if (!std::isinf(_cameraOverridePos.X)) {
			overridePosX = true;
			focusPos.X = _cameraOverridePos.X;
			if (focusPos.X <= 0.0f) {
				focusPos.X = halfView.X - focusPos.X;
			}
		}
		if (!std::isinf(_cameraOverridePos.Y)) {
			overridePosY = true;
			focusPos.Y = _cameraOverridePos.Y;
			if (focusPos.Y <= 0.0f) {
				focusPos.Y = halfView.Y - 1.0f - focusPos.Y;
			}
		}*/

		// Smooth, cumulative look-ahead. The camera base is locked to the player (see the clamp below), so the camera
		// never lags behind - this is purely a lead in the direction of travel. The lead eases toward a target that is
		// proportional to how much the player's speed exceeds CameraStickSpeed (so it builds up and decays gradually),
		// and is applied rounded to whole pixels (clamp below) so the player never shimmers - only clean 1px steps.
		// A player that has speed but didn't actually move (pushing into a wall) gets no lead.
		Vector2f focusSpeed = _targetActor->GetSpeed();
		Vector2f actualMovement = focusPos - _cameraLastPos;
		_cameraLastPos = focusPos;
		if (overridePosX || std::abs(actualMovement.X) < 0.01f) {
			focusSpeed.X = 0.0f;
		}
		if (overridePosY || std::abs(actualMovement.Y) < 0.01f) {
			focusSpeed.Y = 0.0f;
		}

		Vector2f focusVelocity = Vector2f(std::abs(focusSpeed.X), std::abs(focusSpeed.Y));
		float maxLookAheadX = halfView.X * MaxLookAheadFraction;
		float maxLookAheadY = halfView.Y * MaxLookAheadFraction;
		float targetLookAheadX = (focusVelocity.X > CameraStickSpeed
			? (focusSpeed.X < 0.0f ? -1.0f : 1.0f) * std::min((focusVelocity.X - CameraStickSpeed) * LookAheadFactorX, maxLookAheadX) : 0.0f);
		float targetLookAheadY = (focusVelocity.Y > CameraStickSpeed
			? (focusSpeed.Y < 0.0f ? -1.0f : 1.0f) * std::min((focusVelocity.Y - CameraStickSpeed) * LookAheadFactorY, maxLookAheadY) : 0.0f);
		// Ease the look-ahead toward its target, but once the player has stopped (target zero) and the remaining lead
		// is small, freeze it instead of crawling all the way to zero: the camera is pixel-snapped, so easing through
		// the last few pixels shows up as visible 1px jumps. Holding a small constant lead avoids that; easing resumes
		// the moment the player moves again (target != 0).
		static constexpr float LookAheadFreezeThreshold = 4.0f;
		if (targetLookAheadX != 0.0f || std::abs(_cameraDistanceFactor.X) >= LookAheadFreezeThreshold) {
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
		if (overridePosX) {
			_cameraPos.X = focusPos.X + _shakeOffset.X;
		} else if (_viewBounds.W > halfView.X * 2) {
			_cameraPos.X = std::clamp(focusPos.X + std::round(_cameraDistanceFactor.X), _viewBounds.X + halfView.X, _viewBounds.X + _viewBounds.W - halfView.X) + _shakeOffset.X;
			if (!PreferencesCache::UnalignedViewport) {
				_cameraPos.X = std::floor(_cameraPos.X);
			}
		} else {
			_cameraPos.X = std::floor(_viewBounds.X + _viewBounds.W * 0.5f + _shakeOffset.X);
		}
		if (overridePosY) {
			_cameraPos.Y = focusPos.Y + _shakeOffset.Y;
		} else if (_viewBounds.H > halfView.Y * 2) {
			_cameraPos.Y = std::clamp(_cameraViewCenterY + std::round(_cameraDistanceFactor.Y), _viewBounds.Y + halfView.Y - 1.0f, _viewBounds.Y + _viewBounds.H - halfView.Y - 2.0f) + _shakeOffset.Y;
			if (!PreferencesCache::UnalignedViewport) {
				_cameraPos.Y = std::floor(_cameraPos.Y);
			}
		} else {
			_cameraPos.Y = std::floor(_viewBounds.Y + _viewBounds.H * 0.5f + _shakeOffset.Y);
		}

		_camera->SetView(_cameraPos - halfView.As<float>(), 0.0f, 1.0f);
	}

	void PlayerViewport::ShakeCameraView(float duration)
	{
		if (_shakeDuration < duration) {
			_shakeDuration = duration;
		}
	}

	void PlayerViewport::OverrideCamera(float x, float y, bool topLeft)
	{
		// TODO: Not working correctly on some platforms
		/*if (topLeft) {
			// Use negative values for top-left alignment
			_cameraOverridePos.X = -x;
			_cameraOverridePos.Y = -y;
		} else {
			_cameraOverridePos.X = x;
			_cameraOverridePos.Y = y;
		}*/
	}

	void PlayerViewport::WarpCameraToTarget(bool fast)
	{
		// The camera base is locked to the player, so warping just means placing it on the player and resetting the
		// movement-tracking reference. A non-fast (hard) warp also clears the look-ahead so it starts centered.
		Vector2f focusPos = _targetActor->GetPos();
		_cameraPos = focusPos;
		_cameraLastPos = focusPos;
		_cameraViewCenterY = focusPos.Y;
		if (!fast) {
			_cameraDistanceFactor = Vector2f(0.0f, 0.0f);
		}
	}
}