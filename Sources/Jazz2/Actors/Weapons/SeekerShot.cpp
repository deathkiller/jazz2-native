#include "SeekerShot.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"
#include "../Explosion.h"
#include "../Enemies/EnemyBase.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/Base/Random.h"
#include "../../../nCine/CommonConstants.h"

#include <float.h>
#include <algorithm>
#include <vector>

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	SeekerShot::SeekerShot()
		: _fired(0), _searchTicks(1.0f), _hasTarget(false), _targetPos(Vector2f::Zero)
	{
	}

	Task<bool> SeekerShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];

		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/Seeker"_s);

		AnimState state = AnimState::Idle;
		_timeLeft = 140.0f * TicksToFrames;
		if ((_upgrades & 0x1) != 0) {
			_strength = 3;
			state |= (AnimState)1;
		} else {
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
		float baseSpeed = ((_upgrades & 0x1) != 0 ? 1.5f : 2.0f) * TickSpeedScale;
		float sinAngle, cosAngle;
		sincosApprox(angleRel, sinAngle, cosAngle);
		_speed.X = (isFacingLeft ? -cosAngle : cosAngle) * baseSpeed;
		_speed.Y = sinAngle * baseSpeed;
		// The player's speed joins the rocket's own until a target takes over
		bool up = (std::abs(sinAngle) > 0.99f);
		if (!up) {
			_speed.X += std::clamp(speed.X, -MaxCarriedSpeed * TickSpeedScale, MaxCarriedSpeed * TickSpeedScale);
		}
		ShortenUpShotLifetime(sinAngle);

		_renderer.SetRotation(angle);
		_renderer.SetDrawEnabled(false);
	}

	void SeekerShot::OnUpdate(float timeMult)
	{
		TileCollisionParams params = { TileDestructType::Weapon, false, WeaponType::Seeker, _strength };
		TryMovement(timeMult, params);
		if (params.TilesDestroyed > 0) {
			if (auto* player = runtime_cast<Player>(_owner.get())) {
				player->AddScore(params.TilesDestroyed * 50);
			}
		}
		if (params.WeaponStrength <= 0) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		FollowNeareastEnemy(timeMult);

		_fired++;
		RevealPastMuzzle(_gunspotPos);

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
		// Only the RF blast throws the player in the original - measured with the ammo count logged, so a
		// trace showing no knockback could be told from one where the shot was never fired, a Seeker
		// detonating one, two or three tiles away moves the player by exactly nothing, and neither does a
		// TNT going off at their feet. There is no shared "explosion pushes the player" mechanic to share,
		// so this reproduces nothing and is left to Reforged, where it is somebody's design choice rather
		// than a claim about the original. See Player::ApplyBlastKnockback() for the one that is measured.
		if (_levelHandler->IsReforged()) {
			_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, 36.0f, [this](ActorBase* actor) {
				if (auto* player = runtime_cast<Player>(actor)) {
					bool pushLeft = (_pos.X > player->GetPos().X);
					player->AddExternalForce(pushLeft ? -8.0f : 8.0f, 0.0f);
					_levelHandler->HandlePlayerPushed(player);
				}
				return true;
			});
		}

		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.GetLayer() + 2), Explosion::Type::Large);

		return ShotBase::OnPerish(collider);
	}

	void SeekerShot::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void SeekerShot::OnRicochet()
	{
	}

	bool SeekerShot::HasClearLine(Vector2f target) const
	{
		auto* tiles = _levelHandler->TileMap();
		if (tiles == nullptr) {
			return true;
		}

		// About 8 px per sample, at least 2
		std::int32_t Steps = std::max<std::int32_t>((std::int32_t)std::ceil((target - _pos).Length() / 8.0f), 2);
		Vector2f step = (target - _pos) / float(Steps);
		TileCollisionParams params = { TileDestructType::None, false };
		for (std::int32_t i = 1; i <= Steps; i++) {
			Vector2f p = _pos + step * float(i);
			if (!tiles->IsTileEmpty(AABBf(p.X - 0.5f, p.Y - 0.5f, p.X + 0.5f, p.Y + 0.5f), params)) {
				return false;
			}
		}
		return true;
	}

	void SeekerShot::FollowNeareastEnemy(float timeMult)
	{
		// The original looks again every tick, the nearest enemy with a clear line wins
		// The search runs once per original tick, the last result is reused in between
		_searchTicks += timeMult * (70.0f / 60.0f);
		if (_searchTicks >= 1.0f) {
			_searchTicks -= std::floor(_searchTicks);

			static constexpr std::int32_t Capacity = 16;
			float candDist2[Capacity];
			Vector2f candPos[Capacity];
			std::int32_t count = 0;
			_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, TargetRange, [this, &candDist2, &candPos, &count](ActorBase* actor) {
				if (actor->CanCauseDamage(this)) {
					Vector2f newPos = actor->GetPos();
					float d2 = (_pos - newPos).SqrLength();
					if (d2 < TargetRange * TargetRange) {
						if (count < Capacity) {
							candDist2[count] = d2;
							candPos[count] = newPos;
							count++;
						} else {
							// Full, replace the farthest one if this is nearer
							std::int32_t worst = 0;
							for (std::int32_t i = 1; i < Capacity; i++) {
								if (candDist2[i] > candDist2[worst]) {
									worst = i;
								}
							}
							if (d2 < candDist2[worst]) {
								candDist2[worst] = d2;
								candPos[worst] = newPos;
							}
						}
					}
				}
				return true;
			});

			_hasTarget = false;
			while (count > 0) {
				std::int32_t best = 0;
				for (std::int32_t i = 1; i < count; i++) {
					if (candDist2[i] < candDist2[best]) {
						best = i;
					}
				}
				if (HasClearLine(candPos[best])) {
					_hasTarget = true;
					_targetPos = candPos[best];
					break;
				}
				count--;
				candDist2[best] = candDist2[count];
				candPos[best] = candPos[count];
			}
		}

		if (_hasTarget) {
			Vector2f delta = _targetPos - _pos;
			Vector2f pull;
			float distance = delta.Length();
			if (distance <= 4.0f) {
				pull = delta;
			} else if (distance < PullLinearDistance) {
				pull = delta * (PullLength / distance);
			} else {
				pull = delta * PullLinearFactor;
			}

			float keep = std::pow(SpeedKeep, timeMult * TickSpeedScale);
			Vector2f speedPerTick = _speed / TickSpeedScale;
			speedPerTick = speedPerTick * keep + (pull / 7.0f) * (1.0f - keep);
			_speed = speedPerTick * TickSpeedScale;
		}

		if (_speed.X < 0.0f) {
			SetFacingLeft(true);
			_renderer.SetRotation(atan2Approx(-_speed.Y, -_speed.X));
		} else {
			SetFacingLeft(false);
			_renderer.SetRotation(atan2Approx(_speed.Y, _speed.X));
		}
	}
}