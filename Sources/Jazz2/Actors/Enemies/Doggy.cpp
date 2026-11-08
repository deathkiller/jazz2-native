#include "Doggy.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/FreezerShot.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	namespace
	{
		constexpr float WalkSpeed = 1.0f;
		constexpr float DashSpeed = 3.0f;
		constexpr float RunSpeed = 2.0f;
		constexpr float DashTicks = 16.0f;
		constexpr float NoticeDistance = 48.0f;
		constexpr float CalmDownDistance = 160.0f;
	}

	Doggy::Doggy()
		: _attackTime(0.0f), _noiseCooldown(120.0f), _enraged(false), _stuck(false)
	{
	}

	void Doggy::Preload(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		switch (theme) {
			case 0:
			default:
				PreloadMetadataAsync("Enemy/Doggy"_s);
				break;
			case 1: // TSF Cat
				PreloadMetadataAsync("Enemy/Cat"_s);
				break;
		}
	}

	Task<bool> Doggy::OnActivatedAsync(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];

		SetHealthByDifficulty(3);
		_scoreValue = 200;

		SetState(ActorState::CollideWithTilesetReduced, true);

		switch (theme) {
			case 0:
			default:
				async_await RequestMetadataAsync("Enemy/Doggy"_s);
				break;
			case 1: // TSF Cat
				async_await RequestMetadataAsync("Enemy/Cat"_s);
				break;
		}
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Walk);
		UpdateSpeed();

		PlaceOnGround();

		async_return true;
	}

	void Doggy::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (_attackTime > 0.0f) {
			_attackTime -= timeMult;
		}

		if (_enraged && IsPlayerFarAway()) {
			_enraged = false;
		} else if (!_enraged && _attackTime <= 0.0f && IsPlayerInFront()) {
			_enraged = true;
			_attackTime = DashTicks / TicksPerFrame;
			_noiseCooldown = 0.0f;
			PlaySfx("Attack");
		}

		if (IsAttacking()) {
			SetAnimation(AnimState::TransitionAttack);

			if (_noiseCooldown <= 0.0f) {
				_noiseCooldown = Random().NextFloat(25, 40);
				PlaySfx("Woof"_s);
			} else {
				_noiseCooldown -= timeMult;
			}
		} else {
			SetAnimation(AnimState::Walk);

			if (_noiseCooldown <= 0.0f) {
				_noiseCooldown = Random().NextFloat(100, 300);
				PlaySfx("Noise"_s, 0.4f);
			} else {
				_noiseCooldown -= timeMult;
			}
		}
		UpdateSpeed();

		if (GetState(ActorState::CanJump)) {
			if (!CanMoveToPosition(_speed.X * 4.0f, 0.0f)) {
				if (_stuck) {
					MoveInstantly(Vector2f(0.0f, -2.0f), MoveType::Relative | MoveType::Force);
				} else {
					SetFacingLeft(!IsFacingLeft());
					// Measured in JJ2+: every turn while enraged starts a new dash
					if (_enraged) {
						_attackTime = DashTicks / TicksPerFrame;
					}
					UpdateSpeed();
					_stuck = true;
				}
			} else {
				_stuck = false;
			}
		}
	}

	void Doggy::UpdateSpeed()
	{
		float speed = (_attackTime > 0.0f ? DashSpeed : (_enraged ? RunSpeed : WalkSpeed));
		_speed.X = (IsFacingLeft() ? -speed : speed) * TicksPerFrame;
	}

	bool Doggy::IsPlayerInFront() const
	{
		for (auto* player : _levelHandler->GetPlayers()) {
			if (player->GetHealth() <= 0) {
				continue;
			}
			Vector2f diff = player->GetPos() - _pos;
			if (std::abs(diff.X) < NoticeDistance && std::abs(diff.Y) < 32.0f && (diff.X < 0.0f) == IsFacingLeft()) {
				return true;
			}
		}
		return false;
	}

	bool Doggy::IsPlayerFarAway() const
	{
		for (auto* player : _levelHandler->GetPlayers()) {
			if (player->GetHealth() > 0 && std::abs(player->GetPos().X - _pos.X) <= CalmDownDistance) {
				return false;
			}
		}
		return true;
	}

	void Doggy::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteValueAsLE<float>(_attackTime);
		std::uint8_t flags = 0;
		if (_stuck) flags |= 0x01;
		if (_enraged) flags |= 0x02;
		dest.WriteValue<std::uint8_t>(flags);
	}

	void Doggy::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		_attackTime = src.ReadValueAsLE<float>();
		std::uint8_t flags = src.ReadValue<std::uint8_t>();
		_stuck = ((flags & 0x01) != 0);
		_enraged = ((flags & 0x02) != 0);
	}

	void Doggy::OnUpdateHitbox()
	{
		UpdateHitbox(50, 30);
	}

	bool Doggy::OnHandleCollision(ActorBase* other)
	{
		if (auto* shotBase = runtime_cast<Weapons::ShotBase>(other)) {
			DecreaseHealth(shotBase->GetStrength(), shotBase);

			if (_health <= 0.0f) {
				return true;
			}

			HandleFrozenStateChange(shotBase);

			if (shotBase->GetFrozenDuration() <= 0.0f) {
				// Measured in JJ2+: it turns to the shooter and dashes at 3 px/tick for 16 ticks
				if (Player* owner = shotBase->GetOwner()) {
					SetFacingLeft(owner->GetPos().X < _pos.X);
				}
				if (!IsAttacking()) {
					PlaySfx("Attack");
					SetAnimation(AnimState::TransitionAttack);
				}
				_attackTime = DashTicks / TicksPerFrame;
				_noiseCooldown = 45.0f;
				UpdateSpeed();
			}
		}

		return false;
	}

	bool Doggy::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}
