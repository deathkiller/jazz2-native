#include "Explosion.h"
#include "../ILevelHandler.h"

#include "../../nCine/Base/Random.h"

namespace Jazz2::Actors
{
	Explosion::Explosion()
		: _lightBrightness(0.0f), _lightIntensity(0.0f), _lightRadiusNear(0.0f), _lightRadiusFar(0.0f), _scale(1.0f), _time(0.0f)
	{
	}

	void Explosion::Create(ILevelHandler* levelHandler, const Vector3i& pos, Type type, float scale)
	{
		std::shared_ptr<Explosion> explosion = std::make_shared<Explosion>();
		std::uint8_t explosionParams[8];
		*(std::uint16_t*)&explosionParams[0] = (uint16_t)type;
		// 2-3: unused
		*(float*)&explosionParams[4] = scale;
		explosion->OnActivated(ActorActivationDetails(
			levelHandler,
			pos,
			explosionParams
		));
		levelHandler->AddActor(explosion);
	}

	Task<bool> Explosion::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_type = (Type)*(std::uint16_t*)&details.Params[0];
		_scale = *(float*)&details.Params[4];

		SetState(ActorState::ForceDisableCollisions, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Common/Explosions"_s);

		// IceShrapnels are randomized below
		if (_type != Type::IceShrapnel) {
			SetAnimation((AnimState)_type);
		}

		switch (_type) {
			case Type::Large: {
				_lightIntensity = 0.8f;
				_lightBrightness = 0.9f;
				_lightRadiusFar = 55.0f * _scale;
				break;
			}
			case Type::Pepper: {
				_lightIntensity = 0.5f;
				_lightBrightness = 0.2f;
				_lightRadiusNear = 7.0f * _scale;
				_lightRadiusFar = 14.0f * _scale;
				break;
			}
			case Type::RF: {
				_lightIntensity = 0.2f;
				_lightBrightness = 0.2f;
				_lightRadiusFar = 50.0f * _scale;
				break;
			}
			case Type::RFUpgraded: {
				_lightIntensity = 0.2f;
				_lightBrightness = 0.2f;
				_lightRadiusFar = 18.0f * _scale;
				break;
			}
			case Type::IceShrapnel: {
				SetAnimation((AnimState)((std::uint32_t)Type::IceShrapnel + Random().Fast(0, 4)));

				SetState(ActorState::CollideWithTileset | ActorState::ApplyGravitation | ActorState::SkipPerPixelCollisions, true);
				SetState(ActorState::ForceDisableCollisions, false);

				_renderer.Initialize(ActorRendererType::FrozenMask);
				_renderer.CurrentFrame = _renderer.FirstFrame + Random().Fast(0, _renderer.FrameCount);
				_renderer.setAlphaF(1.0f);
				float speedX = Random().FastFloat(0.5f, 2.0f);
				_renderer.AnimDuration /= speedX * 0.7f;
				_speed = Vector2f(speedX * _scale * (Random().NextBool() ? -1.0f : 1.0f), _scale * Random().FastFloat(-4.0f, -1.0f));
				_pos += _speed * _scale * _scale;
				_elasticity = 0.2f;
				SetFacingLeft(_speed.X < 0.0f);
				break;
			}
			case Type::Generator: {
				// Apply random orientation
				_renderer.setRotation(Random().NextFloat(0.0f, 4.0f * fPiOver2));
				SetFacingLeft(Random().NextFloat() < 0.5f);
				break;
			}
		}

		_renderer.setScale(_scale);

		async_return true;
	}

	void Explosion::OnUpdate(float timeMult)
	{
		_time += timeMult;

		switch (_type) {
			case Type::Large: {
				_lightRadiusFar -= timeMult * _scale * 5.0f;
				break;
			}
			case Type::Pepper: {
				_lightIntensity -= timeMult * 0.05f;
				break;
			}
			case Type::RF: {
				float phase1 = _time * 0.16f;
				float phase2 = (phase1 < fRadAngle270 ? sinf(phase1) : -1.0f);
				_lightIntensity = 0.2f + phase2;
				_lightBrightness = 0.2f + phase2;
				_lightRadiusFar = 50.0f * _scale * (1.0f + phase2);
				break;
			}
			case Type::RFUpgraded: {
				float phase1 = _time * 0.18f;
				float phase2 = (phase1 < fRadAngle270 ? sinf(phase1) : -1.0f);
				_lightIntensity = 0.1f + phase2 * 0.1f;
				_lightBrightness = 0.2f + phase2 * 0.4f;
				_lightRadiusNear = (2.0f * _scale) + (phase1 * 1.4f);
				_lightRadiusFar = (16.0f * _scale) + (phase1 * 5.0f);
				break;
			}
			case Type::IceShrapnel: {
				ActorBase::OnUpdate(timeMult);

				float newAlpha = _renderer.alpha() - (0.014f * timeMult);
				if (newAlpha > 0.0f) {
					_renderer.setAlphaF(newAlpha);
					_renderer.setScale(std::min(_scale * newAlpha * 2.0f, _scale));
				} else {
					DecreaseHealth(INT32_MAX);
				}
				break;
			}
		}
	}

	void Explosion::OnUpdateHitbox()
	{
		UpdateHitbox(2, 2);
	}

	void Explosion::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_lightRadiusFar > 0.0f) {
			auto& light1 = lights.emplace_back();
			light1.Pos = _pos;
			light1.Intensity = _lightIntensity;
			light1.Brightness = _lightBrightness;
			light1.RadiusNear = _lightRadiusNear;
			light1.RadiusFar = _lightRadiusFar;
		}

		if (_type == Type::RFUpgraded) {
			// RFUpgraded explosion uses ring light
			float distance = 20.0f + (_time * 6.0f);
			std::int32_t SegmentCount = (std::int32_t)(distance * 0.5f);
			for (std::int32_t i = 0; i < SegmentCount; i++) {
				float angle = i * fTwoPi / SegmentCount;

				auto& light = lights.emplace_back();
				light.Pos = _pos + Vector2f(cosf(angle) * distance, sinf(angle) * distance);
				light.Intensity = _lightIntensity;
				light.Brightness = _lightBrightness;
				light.RadiusNear = _lightRadiusNear;
				light.RadiusFar = _lightRadiusFar;
			}
		} else if (_type == Type::Pepper) {
			auto& light2 = lights.emplace_back();
			light2.Pos = _pos;
			light2.Intensity = 0.1f;
			light2.RadiusNear = 0.0f;
			light2.RadiusFar = 160.0f * _scale;
		}
	}

	void Explosion::OnAnimationFinished()
	{
		if (_type != Type::IceShrapnel) {
			DecreaseHealth(INT32_MAX);
		}
	}
}