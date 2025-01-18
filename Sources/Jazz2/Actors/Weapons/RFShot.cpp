#include "RFShot.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"
#include "../Explosion.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	RFShot::RFShot()
		: _fired(0), _smokeTimer(3.0f)
	{
	}

	Task<bool> RFShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];
		_strength = 2;

		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/RF"_s);

		AnimState state = AnimState::Idle;
		if ((_upgrades & 0x1) != 0) {
			_timeLeft = 24;
			state |= (AnimState)1;
		} else {
			_timeLeft = 30;
		}

		SetAnimation(state);
		PlaySfx("Fire"_s, 0.4f);

		async_return true;
	}

	void RFShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);

		float baseSpeed = ((_upgrades & 0x1) != 0 ? 6.4f : 6.0f);
		if (isFacingLeft) {
			_speed.X = std::min(0.0f, speed.X) - cosf(angleRel) * baseSpeed;
		} else {
			_speed.X = std::max(0.0f, speed.X) + cosf(angleRel) * baseSpeed;
		}
		_speed.Y = sinf(angleRel) * baseSpeed;

		_renderer.setRotation(angle);
		_renderer.setDrawEnabled(false);
	}

	void RFShot::OnUpdate(float timeMult)
	{
		std::int32_t n = (timeMult > 0.9f ? 2 : 1);
		TileCollisionParams params = { TileDestructType::Weapon, false, WeaponType::RF, _strength };
		for (std::int32_t i = 0; i < n && params.WeaponStrength > 0; i++) {
			TryMovement(timeMult / n, params);
		}
		if (params.TilesDestroyed > 0) {
			if (auto* player = runtime_cast<Player*>(_owner)) {
				player->AddScore(params.TilesDestroyed * 50);
			}
		}
		if (params.WeaponStrength <= 0) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		ShotBase::OnUpdate(timeMult);

		if (_smokeTimer > 0.0f) {
			_smokeTimer -= timeMult;
		} else {
			Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() + 2), Explosion::Type::TinyBlue);
			_smokeTimer = 6.0f;
		}

		_fired++;
		if (_fired == 2) {
			MoveInstantly(_gunspotPos, MoveType::Absolute | MoveType::Force);
			_renderer.setDrawEnabled(true);
		}
	}

	void RFShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_fired >= 2) {
			auto& light1 = lights.emplace_back();
			light1.Pos = _pos;
			light1.Intensity = 0.4f;
			light1.Brightness = 0.2f;
			light1.RadiusNear = 0.0f;
			light1.RadiusFar = 60.0f;

			auto& light2 = lights.emplace_back();
			light2.Pos = _pos;
			light2.Intensity = 0.8f;
			light2.Brightness = 0.8f;
			light2.RadiusNear = 3.0f;
			light2.RadiusFar = 14.0f;

		}
	}

	bool RFShot::OnPerish(ActorBase* collider)
	{
		_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, 36.0f, [this](ActorBase* actor) {
			if (auto* player = runtime_cast<Player*>(actor)) {
				bool pushLeft = (_pos.X > player->GetPos().X);
				player->AddExternalForce(pushLeft ? -4.0f : 4.0f, 0.0f);
			}
			return true;
		});

		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.layer() + 2),
			(_upgrades & 0x1) != 0 ? Explosion::Type::RFUpgraded : Explosion::Type::RF);

		PlaySfx("Explode"_s, 0.6f);

		return ShotBase::OnPerish(collider);
	}

	void RFShot::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void RFShot::OnRicochet()
	{
	}
}