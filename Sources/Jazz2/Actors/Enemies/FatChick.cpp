#include "FatChick.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	FatChick::FatChick()
		: _isAttacking(false), _stuck(false), _attackCooldown(AttackWaitTicks / TicksPerFrame)
	{
	}

	void FatChick::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/FatChick"_s);
	}

	Task<bool> FatChick::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(3);
		_scoreValue = 200;

		async_await RequestMetadataAsync("Enemy/FatChick"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Walk);

		_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;

		PlaceOnGround();

		async_return true;
	}

	void FatChick::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		Vector2f targetPos;
		// The original turns toward a close player only above Easy
		if (!_isAttacking && _levelHandler->GetDifficulty() != GameDifficulty::Easy && FindNearestPlayer(TurnDistance, targetPos) &&
			std::abs(_pos.X - targetPos.X) > TurnDeadZone && IsFacingLeft() != (_pos.X > targetPos.X)) {
			SetFacingLeft(_pos.X > targetPos.X);
			_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
		}

		if (GetState(ActorState::CanJump)) {
			if (!CanMoveToPosition(_speed.X * 4, 0)) {
				if (_stuck) {
					MoveInstantly(Vector2f(0.0f, -2.0f), MoveType::Relative | MoveType::Force);
				} else {
					SetFacingLeft(!IsFacingLeft());
					_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
					_stuck = true;
				}
			} else {
				_stuck = false;
			}
		}

		if (!_isAttacking) {
			if (_attackCooldown > 0.0f) {
				_attackCooldown -= timeMult;
			} else if (FindNearestPlayer(AttackDistance, targetPos) && IsFacingLeft() == (targetPos.X < _pos.X)) {
				Attack();
			}
		}
	}

	bool FatChick::FindNearestPlayer(float maxDistance, Vector2f& targetPos) const
	{
		Player* player = EnemyBase::FindNearestPlayer(maxDistance * maxDistance);
		if (player != nullptr) {
			targetPos = player->GetPos();
		}
		return (player != nullptr);
	}

	void FatChick::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		std::uint8_t flags = 0;
		if (_isAttacking) flags |= 0x01;
		if (_stuck) flags |= 0x02;
		dest.WriteValue<std::uint8_t>(flags);
	}

	void FatChick::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		std::uint8_t flags = src.ReadValue<std::uint8_t>();
		_isAttacking = ((flags & 0x01) != 0);
		_stuck = ((flags & 0x02) != 0);

		if (_isAttacking) {
			// The attack transition was interrupted, finish it as its callback would
			_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
			_isAttacking = false;
		}
	}

	void FatChick::OnUpdateHitbox()
	{
		UpdateHitbox(20, 24);
	}

	bool FatChick::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	void FatChick::Attack()
	{
		// TODO: Play sound in the middle of transition
		// TODO: Apply force in the middle of transition
		PlaySfx("Attack"_s, 0.8f, 0.6f);

		SetTransition(AnimState::TransitionAttack, false, [this]() {
			_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
			_isAttacking = false;
			_attackCooldown = AttackWaitTicks / TicksPerFrame;
		});
		_speed.X = 0.0f;
		_isAttacking = true;
	}
}