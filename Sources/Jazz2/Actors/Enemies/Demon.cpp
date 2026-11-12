#include "Demon.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Demon::Demon()
		: _attackTime(IdleTicks / TicksPerFrame), _walkTime(0.0f), _lastIdleFrame(-1), _attacking(false), _stuck(false)
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
			// Measured in JJ2+: it slides to a stop only while its stop animation plays
			if (_currentTransition != nullptr) {
				_speed.X *= powf(BrakeFactor, timeMult * TicksPerFrame);
			} else {
				_speed.X = 0.0f;
			}
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

			// Measured in JJ2+: after a while of walking it stops as soon as nobody is near
			_walkTime += timeMult;
			if (_walkTime > MinWalkTicks / TicksPerFrame && FindNearestPlayer(StopDistance * StopDistance) == nullptr) {
				_attacking = false;
				_attackTime = IdleTicks / TicksPerFrame;

				SetAnimation(AnimState::Idle);
				SetTransition((AnimState)1073741826, false);
			}
		} else {
			// The idle loop rings once a cycle
			std::int32_t frame = _renderer.CurrentFrame - _renderer.FirstFrame;
			if (frame == IdleBellFrame && _lastIdleFrame != IdleBellFrame) {
				PlaySfx("Idle"_s, 0.4f);
			}
			_lastIdleFrame = frame;

			if (_attackTime > 0.0f) {
				_attackTime -= timeMult;
			} else if (Player* player = FindNearestPlayer(NoticeDistance * NoticeDistance)) {
				// It always turns to the player in range, but charges only one at about its own height
				Vector2f diff = player->GetPos() - _pos;
				if (diff.X != 0.0f) {
					SetFacingLeft(diff.X < 0.0f);
				}
				if (std::abs(diff.Y) < NoticeHeight) {
					_attacking = true;
					_walkTime = 0.0f;
					_speed.X = 0.0f;

					PlaySfx("AttackStart"_s, 0.6f);
					SetAnimation((AnimState)1073741824);
					SetTransition((AnimState)1073741825, false, [this]() {
						PlaySfx("Attack"_s, 0.6f);
					});
				}
			}
		}
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