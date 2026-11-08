#include "Demon.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Demon::Demon()
		: _attackTime(IdleTicks / TicksPerFrame), _walkTime(0.0f), _attacking(false), _stuck(false)
	{
	}

	void Demon::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Demon"_s);
	}

	Task<bool> Demon::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(2);
		_scoreValue = 100;

		SetState(ActorState::CollideWithTilesetReduced, true);

		async_await RequestMetadataAsync("Enemy/Demon"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Idle);

		PlaceOnGround();

		async_return true;
	}

	void Demon::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (!_attacking) {
			// Measured in JJ2+: it slides to a stop
			_speed.X *= powf(BrakeFactor, timeMult * TicksPerFrame);
		}

		if (_currentTransition != nullptr) {
			return;
		}

		if (_attacking) {
			_speed.X = (IsFacingLeft() ? -WalkSpeed : WalkSpeed) * TicksPerFrame;

			if (GetState(ActorState::CanJump)) {
				if (!CanMoveToPosition(_speed.X * 4.0f, 0.0f)) {
					if (_stuck) {
						MoveInstantly(Vector2f(0.0f, -2.0f), MoveType::Relative | MoveType::Force);
					} else {
						SetFacingLeft(!IsFacingLeft());
						_speed.X = (IsFacingLeft() ? -WalkSpeed : WalkSpeed) * TicksPerFrame;
						_walkTime = 0.0f;
						_stuck = true;
					}
				} else {
					_stuck = false;
				}
			}

			// Measured in JJ2+: it stops only after walking away from the player for a while
			_walkTime += timeMult;
			if (_walkTime >= MinWalkAwayTicks / TicksPerFrame && IsWalkingAwayFromPlayer()) {
				_attacking = false;
				_attackTime = IdleTicks / TicksPerFrame;

				SetAnimation(AnimState::Idle);
				SetTransition((AnimState)1073741826, false);
			}
		} else if (_attackTime > 0.0f) {
			_attackTime -= timeMult;
		} else {
			for (auto& player : _levelHandler->GetPlayers()) {
				if (player->GetHealth() <= 0) {
					continue;
				}

				Vector2f newPos = player->GetPos();
				if ((newPos - _pos).Length() < 256.0f) {
					_attacking = true;
					_walkTime = 0.0f;

					SetFacingLeft(newPos.X < _pos.X);
					_speed.X = 0.0f;

					SetAnimation((AnimState)1073741824);
					SetTransition((AnimState)1073741825, false);
					break;
				}
			}
		}
	}

	bool Demon::IsWalkingAwayFromPlayer() const
	{
		bool found = false;
		for (auto& player : _levelHandler->GetPlayers()) {
			if (player->GetHealth() <= 0) {
				continue;
			}
			float dx = player->GetPos().X - _pos.X;
			if (std::abs(dx) <= StopDistance || (dx < 0.0f) == IsFacingLeft()) {
				return false;
			}
			found = true;
		}
		return found;
	}

	void Demon::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteValueAsLE<float>(_attackTime);
		dest.WriteValueAsLE<float>(_walkTime);

		std::uint8_t flags = 0;
		if (_attacking) flags |= 0x01;
		if (_stuck) flags |= 0x02;
		dest.WriteValue<std::uint8_t>(flags);
	}

	void Demon::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		_attackTime = src.ReadValueAsLE<float>();
		_walkTime = src.ReadValueAsLE<float>();

		std::uint8_t flags = src.ReadValue<std::uint8_t>();
		_attacking = ((flags & 0x01) != 0);
		_stuck = ((flags & 0x02) != 0);
	}

	void Demon::OnUpdateHitbox()
	{
		UpdateHitbox(28, 26);
	}

	bool Demon::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}