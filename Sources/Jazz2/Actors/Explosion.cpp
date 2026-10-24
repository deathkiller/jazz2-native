#include "Explosion.h"
#include "../ILevelHandler.h"
#include "../PreferencesCache.h"

#include "../../nCine/Base/Random.h"

namespace Jazz2::Actors
{
	Explosion::Explosion()
		: _lightBrightness(0.0f), _lightIntensity(0.0f), _lightRadiusNear(0.0f), _lightRadiusFar(0.0f), _scale(1.0f), _time(0.0f)
	{
	}

	void Explosion::Create(ILevelHandler* levelHandler, const Vector3i& pos, Type type, float scale, Vector2f speed)
	{
		// Smoke puffs are purely decorative, but they are spawned in bursts (a destroyed crate alone makes four),
		// and each one is a full actor with its own animation and render command - drop them when the user turned
		// particles off, the same as the debris produced by the tilemap
		if (PreferencesCache::Particles == ParticleQuality::Off) {
			switch (type) {
				case Type::SmokeBrown:
				case Type::SmokeGray:
				case Type::SmokeWhite:
				case Type::SmokePoof:
					return;
				default:
					break;
			}
		}

		std::shared_ptr<Explosion> explosion = std::make_shared<Explosion>();
		std::uint8_t explosionParams[8];
		EventParamsWriter writer(explosionParams);
		writer.SetUint16(0, (std::uint16_t)type);
		// 2-3: unused
		writer.SetFloat(4, scale);
		explosion->OnActivated(ActorActivationDetails(
			levelHandler,
			pos,
			explosionParams
		));
		if (type == Type::IceShrapnel) {
			explosion->_speed += speed;
		}
		levelHandler->AddActor(explosion);
	}

	Task<bool> Explosion::OnActivatedAsync(const ActorActivationDetails& details)
	{
		EventParamsReader params(details);
		_type = (Type)params.GetUint16(0);
		_scale = params.GetFloat(4);

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
				float phase2 = (phase1 < fRadAngle270 ? sinApprox(phase1) : -1.0f);
				_lightIntensity = 0.2f + phase2;
				_lightBrightness = 0.2f + phase2;
				_lightRadiusFar = 50.0f * _scale * (1.0f + phase2);
				break;
			}
			case Type::RFUpgraded: {
				float phase1 = _time * 0.18f;
				float phase2 = (phase1 < fRadAngle270 ? sinApprox(phase1) : -1.0f);
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
		// A light whose intensity and brightness have both ramped to nothing adds nothing - both lighting paths clamp
		// a light's contribution at zero - but it still costs its whole circle. The upgraded RF explosion ramps both
		// through zero about halfway through its animation, while its ring keeps growing for the rest of it.
		const bool lightVisible = (_lightIntensity > 0.0f || _lightBrightness > 0.0f);

		if (_lightRadiusFar > 0.0f && lightVisible) {
			auto& light1 = lights.emplace_back();
			light1.Pos = _pos;
			light1.Intensity = _lightIntensity;
			light1.Brightness = _lightBrightness;
			light1.RadiusNear = _lightRadiusNear;
			light1.RadiusFar = _lightRadiusFar;
		}

		if (_type == Type::RFUpgraded) {
			// RFUpgraded explosion uses a ring light. It used to be a chain of point lights, one every 4*pi px of the
			// circumference - up to 88 large ones per explosion, and any sparser chain showed the gaps between them
			// on the edges of the band. A single ring light draws only the band and has no gaps at all. Its cross-
			// section is what that chain summed to: the falloff integrated along the ring and divided by the 4*pi
			// spacing, (near + (far - near) / 4) / (2 * pi) at the peak, and a near radius 3/4 of a single light's
			// gives the same shape to within a few percent (the remaining difference is the curvature, which made
			// the inside of the old chain slightly denser than its outside).
			if (lightVisible) {
				const float strength = (_lightRadiusNear + (_lightRadiusFar - _lightRadiusNear) * 0.25f) / fTwoPi;

				auto& light = lights.emplace_back();
				light.Pos = _pos;
				light.Intensity = _lightIntensity * strength;
				light.Brightness = _lightBrightness * strength;
				light.RadiusNear = _lightRadiusNear * 0.75f;
				light.RadiusFar = _lightRadiusFar;
				light.RingRadius = 20.0f + (_time * 6.0f);
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