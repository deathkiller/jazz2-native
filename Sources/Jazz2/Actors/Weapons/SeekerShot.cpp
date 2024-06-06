#include "SeekerShot.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"
#include "../Explosion.h"
#include "../Enemies/EnemyBase.h"

#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/Base/Random.h"
#include "../../../nCine/CommonConstants.h"

#include <float.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	SeekerShot::SeekerShot()
		: _fired(0), _followRecomputeTime(0.0f)
	{
	}

	Task<bool> SeekerShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];

		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/Seeker"_s);

		AnimState state = AnimState::Idle;
		if ((_upgrades & 0x1) != 0) {
			_timeLeft = 188.0f;
			_defaultRecomputeTime = 6.0f;
			_strength = 3;
			state |= (AnimState)1;
		} else {
			_timeLeft = 144.0f;
			_defaultRecomputeTime = 10.0f;
			_strength = 2;
		}

		SetAnimation(state);
		PlaySfx("Fire"_s);

		async_return true;
	}

	void SeekerShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);

		// Upgraded rockets are slower
		float baseSpeed = ((_upgrades & 0x1) != 0 ? 1.7f : 1.85f);
		if (isFacingLeft) {
			_speed.X = std::min(0.0f, speed.X * 0.06f) - cosf(angleRel) * baseSpeed;
		} else {
			_speed.X = std::max(0.0f, speed.X * 0.06f) + cosf(angleRel) * baseSpeed;
		}
		_speed.Y = sinf(angleRel) * baseSpeed;

		_renderer.setRotation(angle);
		_renderer.setDrawEnabled(false);
	}

	void SeekerShot::OnUpdate(float timeMult)
	{
		TileCollisionParams params = { TileDestructType::Weapon, false, WeaponType::Seeker, _strength };
		TryMovement(timeMult, params);
		if (params.WeaponStrength <= 0) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		FollowNeareastEnemy(timeMult);

		_fired++;
		if (_fired == 2) {
			MoveInstantly(_gunspotPos, MoveType::Absolute | MoveType::Force);
			_renderer.setDrawEnabled(true);
		}

		ShotBase::OnUpdate(timeMult);
	}

	void SeekerShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_fired >= 2) {
			auto& light = lights.emplace_back();
			light.Pos = _pos;
			light.Intensity = 0.8f;
			light.RadiusNear = 3.0f;
			light.RadiusFar = 10.0f;
		}
	}

	bool SeekerShot::OnPerish(ActorBase* collider)
	{
		_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, 36.0f, [this](ActorBase* actor) {
			if (auto* player = runtime_cast<Player*>(actor)) {
				bool pushLeft = (_pos.X > player->GetPos().X);
				player->AddExternalForce(pushLeft ? -8.0f : 8.0f, 0.0f);
			}
			return true;
		});

		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + _speed.X), (int)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::Large);

		return ShotBase::OnPerish(collider);
	}

	void SeekerShot::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void SeekerShot::OnRicochet()
	{
	}

	void SeekerShot::FollowNeareastEnemy(float timeMult)
	{
		if (_followRecomputeTime > 0.0f) {
			_followRecomputeTime -= timeMult;
			return;
		}

		Vector2f targetPos = Vector2f(FLT_MAX, FLT_MAX);
		float targetDistance = FLT_MAX;

		// Max. distance is ~8 tiles
		_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, 260.0f, [this, &targetPos, &targetDistance](ActorBase* actor) {
			if (auto* enemyBase = runtime_cast<Enemies::EnemyBase*>(actor)) {
				if (!enemyBase->IsInvulnerable() && enemyBase->CanCollideWithAmmo) {
					Vector2f newPos = enemyBase->GetPos();
					float distance = (_pos - newPos).Length();
					if (distance < 260.0f && distance < targetDistance) {
						targetPos = newPos;
						targetDistance = distance;
					}
				}
			}
			return true;
		});

		if (targetDistance < 260.0f) {
			Vector2f speed = (Vector2f(_speed.X, _speed.Y) + (targetPos - _pos).Normalized() * 2.0f).Normalized();
			_speed.X = speed.X * 2.6f;
			_speed.Y = speed.Y * 2.6f;
		}

		if (_speed.X < 0.0f) {
			SetFacingLeft(true);
			_renderer.setRotation(atan2f(-_speed.Y, -_speed.X));
		} else {
			SetFacingLeft(false);
			_renderer.setRotation(atan2f(_speed.Y, _speed.X));
		}

		_followRecomputeTime = _defaultRecomputeTime;
	}
}