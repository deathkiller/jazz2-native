#include "Fencer.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

namespace Jazz2::Actors::Enemies
{
	Fencer::Fencer()
		: _stateTime(HopBackWaitTicks / TicksPerFrame), _airborne(false)
	{
	}

	void Fencer::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Fencer"_s);
	}

	Task<bool> Fencer::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(3);
		_scoreValue = 400;

		async_await RequestMetadataAsync("Enemy/Fencer"_s);
		SetFacingLeft(true);
		SetAnimation(AnimState::Idle);

		PlaceOnGround();

		async_return true;
	}

	void Fencer::OnUpdate(float timeMult)
	{
		if (_frozenTimeLeft <= 0.0f && !_airborne) {
			Player* target = nullptr;
			float targetDistanceSqr = SightDistance * SightDistance;
			for (auto* player : _levelHandler->GetPlayers()) {
				if (player->GetHealth() <= 0) {
					continue;
				}

				float distanceSqr = (player->GetPos() - _pos).SqrLength();
				if (distanceSqr < targetDistanceSqr) {
					targetDistanceSqr = distanceSqr;
					target = player;
				}
			}

			if (target != nullptr) {
				Vector2f targetPos = target->GetPos();
				if (targetPos.X != _pos.X) {
					SetFacingLeft(_pos.X > targetPos.X);
				}

				if (std::abs(targetPos.Y - _pos.Y) < SightHeight) {
					float dir = (IsFacingLeft() ? -1.0f : 1.0f);
					if (targetDistanceSqr < ShotReactDistance * ShotReactDistance && target->HasFiredWithin(ShotWindowTicks / TicksPerFrame)) {
						// The original hops toward a player who has just fired
						Hop(2.0f * dir, -5.0f, -6.0f);
					} else if (targetDistanceSqr < HopBackDistance * HopBackDistance && _stateTime <= 0.0f &&
						_levelHandler->GetDifficulty() != GameDifficulty::Easy) {
						// Measured in JJ2+: a small hop back from the player at 1 px/tick
						Hop(-1.0f * dir, -2.0f, -4.0f);
					}
				}
			}

			if (_stateTime > 0.0f) {
				_stateTime -= timeMult;
			}
		}

		EnemyBase::OnUpdate(timeMult);
	}

	void Fencer::Hop(float speedX, float speedY, float offsetY)
	{
		_airborne = true;
		_speed.X = speedX * TicksPerFrame;

		MoveInstantly(Vector2f(0.0f, offsetY), MoveType::Relative);
		_speed.Y = speedY * TicksPerFrame;

		PlaySfx("Attack"_s);

		SetTransition(AnimState::TransitionAttack, false);
		// Measured in JJ2+: the hop animation lasts as long as the hop
		SetAnimationTicks(HopAnimTicks);
	}

	void Fencer::OnHitFloor(float timeMult)
	{
		EnemyBase::OnHitFloor(timeMult);

		if (_airborne) {
			_airborne = false;
			_speed.X = 0.0f;
			_stateTime = HopBackWaitTicks / TicksPerFrame;
		}
	}

	float Fencer::GetGravityModifier(float baseGravity, bool isRising) const
	{
		// Measured in JJ2+: the hop rises and falls at the same rate
		return (_airborne ? ReplaceLevelGravity(HopGravity, baseGravity) : baseGravity);
	}

	void Fencer::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteValueAsLE<float>(_stateTime);
		dest.WriteValue<std::uint8_t>(_currentTransition != nullptr ? 1 : 0);
	}

	void Fencer::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		_stateTime = src.ReadValueAsLE<float>();
		bool isAttacking = (src.ReadValue<std::uint8_t>() != 0);
		if (isAttacking) {
			// The attack jump is stopped in its transition callback
			_speed.X = 0.0f;
		}
	}

	bool Fencer::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}