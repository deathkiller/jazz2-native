#include "PepperShot.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Explosion.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	PepperShot::PepperShot()
		: _fired(0)
	{
	}

	Task<bool> PepperShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];
		_strength = 1;

		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/Pepper"_s);

		AnimState state = AnimState::Idle;
		_timeLeft = 55.0f * TicksToFrames;
		if ((_upgrades & 0x01) != 0) {
			state |= (AnimState)1;
		}

		SetAnimation(state);
		PlaySfx("Fire"_s);

		// The plain spray is drawn opaque, the powered-up one is additive
		if ((_upgrades & 0x01) != 0) {
			_renderer.SetBlendingPreset(DrawableNode::BlendingPreset::Additive);
		}

		async_return true;
	}

	void PepperShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);

		// Both variants spray alike, each drop at its own speed
		float baseSpeed = (193.0f + float(Random().Next() % 128)) / 64.0f * TickSpeedScale;
		float sinAngle, cosAngle;
		sincosApprox(angleRel, sinAngle, cosAngle);
		_speed.X = (isFacingLeft ? -cosAngle : cosAngle) * baseSpeed + speed.X;
		_speed.Y = sinAngle * baseSpeed;

		_renderer.SetRotation(angle);
		_renderer.SetDrawEnabled(false);
	}

	void PepperShot::OnUpdate(float timeMult)
	{
		std::int32_t n = GetMovementSubstepCount(timeMult);
		TileCollisionParams params = { TileDestructType::Weapon, false, WeaponType::Pepper, _strength };
		for (std::int32_t i = 0; i < n && params.WeaponStrength > 0; i++) {
			TryMovement(timeMult / n, params);
		}
		if (params.TilesDestroyed > 0) {
			if (auto* player = runtime_cast<Player>(_owner.get())) {
				player->AddScore(params.TilesDestroyed * 50);
			}
		}
		if (params.WeaponStrength <= 0) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		ShotBase::OnUpdate(timeMult);

		if (_fired < 255) {
			_fired++;
		}
		RevealPastMuzzle(_gunspotPos);
	}
	
	void PepperShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_fired >= 2 && (_upgrades & 0x1) != 0) {
			auto& light = lights.emplace_back();
			light.Pos = _pos;
			light.Intensity = 1.0f;
			light.Brightness = 1.0f;
			light.RadiusNear = 4.0f;
			light.RadiusFar = 16.0f;
		}
	}

	bool PepperShot::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.GetLayer() + 2), Explosion::Type::Pepper);

		return ShotBase::OnPerish(collider);
	}

	void PepperShot::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void PepperShot::OnRicochet()
	{
		ShotBase::OnRicochet();

		_renderer.SetRotation(atan2Approx(_speed.Y, _speed.X));
	}
}