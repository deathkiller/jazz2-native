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
			_cameraResponsiveness(ResponsivenessMax, ResponsivenessMax), _shakeDuration(0.0f)
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
			_viewTexture->init(nullptr, Texture::Format::RGB8, w, h);
			_view->SetTexture(_viewTexture.get());
		}

		_viewTexture->setMagFiltering(SamplerFilter::Nearest);
		_viewTexture->setWrap(SamplerWrapping::ClampToEdge);

		_camera->SetOrthoProjection(0.0f, (float)w, (float)h, 0.0f);

		if (notInitialized) {
			_lightingRenderer = std::make_unique<LightingRenderer>(this);
			_lightingBuffer = std::make_unique<Texture>(nullptr, Texture::Format::RG8, w, h);
			_lightingView = std::make_unique<Viewport>(_lightingBuffer.get(), Viewport::DepthStencilFormat::None);
			_lightingView->SetRootNode(_lightingRenderer.get());
			_lightingView->SetCamera(_camera.get());
		} else {
			_lightingView->RemoveAllTextures();
			_lightingBuffer->init(nullptr, Texture::Format::RG8, w, h);
			_lightingView->SetTexture(_lightingBuffer.get());
		}

		_lightingBuffer->setMagFiltering(SamplerFilter::Nearest);
		_lightingBuffer->setWrap(SamplerWrapping::ClampToEdge);

		_downsamplePass.Initialize(_viewTexture.get(), w / 2, h / 2, Vector2f(0.0f, 0.0f));
		_blurPass1.Initialize(_downsamplePass.GetTarget(), w / 2, h / 2, Vector2f(1.0f, 0.0f));
		_blurPass2.Initialize(_blurPass1.GetTarget(), w / 2, h / 2, Vector2f(0.0f, 1.0f));
		_blurPass3.Initialize(_blurPass2.GetTarget(), w / 4, h / 4, Vector2f(1.0f, 0.0f));
		_blurPass4.Initialize(_blurPass3.GetTarget(), w / 4, h / 4, Vector2f(0.0f, 1.0f));

		if (notInitialized) {
			_combineRenderer = std::make_unique<CombineRenderer>(this);
			_combineRenderer->setParent(outputNode);
		}

		_combineRenderer->Initialize(bounds.X, bounds.Y, bounds.W, bounds.H);

		return notInitialized;
	}

	void PlayerViewport::Register()
	{
		_blurPass4.Register();
		_blurPass3.Register();
		_blurPass2.Register();
		_blurPass1.Register();
		_downsamplePass.Register();

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
		return _viewTexture->size();
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

		// If player doesn't move but has some speed, it's probably stuck, so reset the speed
		Vector2f focusSpeed = _targetActor->GetSpeed();
		if (overridePosX || std::abs(_cameraLastPos.X - focusPos.X) < 1.0f) {
			focusSpeed.X = 0.0f;
		}
		if (overridePosY || std::abs(_cameraLastPos.Y - focusPos.Y) < 1.0f) {
			focusSpeed.Y = 0.0f;
		}

		Vector2f focusVelocity = Vector2f(std::abs(focusSpeed.X), std::abs(focusSpeed.Y));

		// Camera responsiveness (smoothing unexpected movements)
		if (focusVelocity.X <= 3.0f) {
			if (_cameraResponsiveness.X > ResponsivenessMin) {
				_cameraResponsiveness.X = std::max(_cameraResponsiveness.X - ResponsivenessChange * timeMult, ResponsivenessMin);
			}
		} else {
			if (_cameraResponsiveness.X < ResponsivenessMax) {
				_cameraResponsiveness.X = std::min(_cameraResponsiveness.X + ResponsivenessChange * timeMult, ResponsivenessMax);
			}
		}
		if (focusVelocity.Y <= 3.0f) {
			if (_cameraResponsiveness.Y > ResponsivenessMin) {
				_cameraResponsiveness.Y = std::max(_cameraResponsiveness.Y - ResponsivenessChange * timeMult, ResponsivenessMin);
			}
		} else {
			if (_cameraResponsiveness.Y < ResponsivenessMax) {
				_cameraResponsiveness.Y = std::min(_cameraResponsiveness.Y + ResponsivenessChange * timeMult, ResponsivenessMax);
			}
		}

		_cameraLastPos.X = lerpByTime(_cameraLastPos.X, focusPos.X, std::min(_cameraResponsiveness.X, 1.0f), timeMult);
		_cameraLastPos.Y = lerpByTime(_cameraLastPos.Y, focusPos.Y, std::min(_cameraResponsiveness.Y, 1.0f), timeMult);

		_cameraDistanceFactor.X = lerpByTime(_cameraDistanceFactor.X, focusSpeed.X * 8.0f, (focusVelocity.X < 2.0f ? SlowRatioX : FastRatioX), timeMult);
		_cameraDistanceFactor.Y = lerpByTime(_cameraDistanceFactor.Y, focusSpeed.Y * 5.0f, (focusVelocity.Y < 2.0f ? SlowRatioY : FastRatioY), timeMult);

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

		// Clamp camera position to level bounds
		if (overridePosX) {
			_cameraPos.X = _cameraLastPos.X + _shakeOffset.X;
		} else if (_viewBounds.W > halfView.X * 2) {
			_cameraPos.X = std::clamp(_cameraLastPos.X + _cameraDistanceFactor.X, _viewBounds.X + halfView.X, _viewBounds.X + _viewBounds.W - halfView.X) + _shakeOffset.X;
			if (!PreferencesCache::UnalignedViewport || std::abs(_cameraDistanceFactor.X) < 1.0f) {
				_cameraPos.X = std::floor(_cameraPos.X);
			}
		} else {
			_cameraPos.X = std::floor(_viewBounds.X + _viewBounds.W * 0.5f + _shakeOffset.X);
		}
		if (overridePosY) {
			_cameraPos.Y = _cameraLastPos.Y + _shakeOffset.Y;
		} else if (_viewBounds.H > halfView.Y * 2) {
			_cameraPos.Y = std::clamp(_cameraLastPos.Y + _cameraDistanceFactor.Y, _viewBounds.Y + halfView.Y - 1.0f, _viewBounds.Y + _viewBounds.H - halfView.Y - 2.0f) + _shakeOffset.Y;
			if (!PreferencesCache::UnalignedViewport || std::abs(_cameraDistanceFactor.Y) < 1.0f) {
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
		Vector2f focusPos = _targetActor->GetPos();
		if (!fast) {
			_cameraPos = focusPos;
			_cameraLastPos = _cameraPos;
			_cameraDistanceFactor = Vector2f(0.0f, 0.0f);
			_cameraResponsiveness = Vector2f(ResponsivenessMax, ResponsivenessMax);
		} else {
			Vector2f diff = _cameraLastPos - _cameraPos;
			_cameraPos = focusPos;
			_cameraLastPos = _cameraPos + diff;
		}
	}
}