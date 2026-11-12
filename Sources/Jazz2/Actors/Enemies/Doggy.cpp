#include "Doggy.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../../Events/EventMap.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/FreezerShot.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	namespace
	{
		// The original's rules, in its ticks and px per tick
		constexpr float WalkSpeed = 1.0f;
		constexpr float DashSpeed = 3.0f;
		constexpr float RunSpeed = 2.0f;
		constexpr float AttackTicks = 255.0f;
		constexpr float BarkTicks = 40.0f;
		// Measured in JJ2+: its feet are 13.5 px below its position
		constexpr float AttackFrontEdge = 34.0f;
		constexpr float AttackWallProbeY = 2.5f;
		constexpr float AttackFloorProbeY = 22.5f;
	}

	Doggy::Doggy()
		: _attackTime(0.0f), _noiseCooldown(0.0f), _stuck(false)
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
		// It always sets off to the right
		SetFacingLeft(false);
		SetAnimation(AnimState::Walk);
		UpdateSpeed();
		ResetSniffTime();

		PlaceOnGround();

		async_return true;
	}

	void Doggy::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		bool wasAttacking = IsAttacking();
		if (wasAttacking) {
			// It runs level, ending the attack when there's a wall, a drop deeper than about 9 px or Stop Enemy ahead
			if (!CanAttackRunOn()) {
				MoveInstantly(Vector2f(-_speed.X * timeMult, 0.0f), MoveType::Relative);
				SetFacingLeft(!IsFacingLeft());
				EndAttack();
			} else {
				_attackTime -= timeMult;
				if (_attackTime <= 0.0f) {
					EndAttack();
				}
			}
		}

		// Only a touch or a shot makes it attack, it doesn't watch for players, a touch during the attack doesn't extend it
		for (auto* player : _levelHandler->GetPlayers()) {
			if (player->GetHealth() > 0 && AABBInner.Overlaps(player->AABBInner)) {
				if (IsAttacking()) {
					_attackTarget = player->shared_from_this();
				} else {
					StartAttack(player);
				}
				break;
			}
		}

		if (IsAttacking()) {
			SetAnimation(AnimState::TransitionAttack);

			// It barks now and then at random
			if (Random().NextFloat() < timeMult * TicksPerFrame / BarkTicks) {
				PlaySfx("Woof"_s);
			}
		} else {
			SetAnimation(AnimState::Walk);

			// It sniffs half of the times its timer runs out
			_noiseCooldown -= timeMult;
			if (_noiseCooldown <= 0.0f) {
				ResetSniffTime();
				if (Random().NextBool()) {
					PlaySfx("Noise"_s, 0.4f);
				}
			}
		}
		UpdateSpeed();

		if (!IsAttacking() && GetState(ActorState::CanJump)) {
			if (!CanMoveToPosition(_speed.X * 4.0f, 0.0f)) {
				if (_stuck) {
					MoveInstantly(Vector2f(0.0f, -2.0f), MoveType::Relative | MoveType::Force);
				} else {
					SetFacingLeft(!IsFacingLeft());
					UpdateSpeed();
					_stuck = true;
				}
			} else {
				_stuck = false;
			}
		}
	}

	void Doggy::StartAttack(Player* player)
	{
		if (!IsAttacking()) {
			PlaySfx("Attack"_s);
			// No gravity while it runs, it keeps its height
			SetState(ActorState::ApplyGravitation, false);
			_speed.Y = 0.0f;
		}
		_attackTime = AttackTicks / TicksPerFrame;
		if (player != nullptr) {
			_attackTarget = player->shared_from_this();
		}
	}

	void Doggy::EndAttack()
	{
		_attackTime = 0.0f;
		_attackTarget.reset();
		SetState(ActorState::ApplyGravitation, true);
	}

	bool Doggy::CanAttackRunOn()
	{
		// Single pixels at its front edge, a wall at about its middle and the floor 9 px below its feet
		float frontX = _pos.X + (IsFacingLeft() ? -AttackFrontEdge : AttackFrontEdge);
		auto events = _levelHandler->EventMap();
		std::uint8_t* eventParams;
		if (events != nullptr && events->GetEventByPosition(frontX, _pos.Y + AttackWallProbeY, &eventParams) == EventType::AreaStopEnemy) {
			return false;
		}

		Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, true };
		float px = std::floor(frontX);
		float wallY = std::floor(_pos.Y + AttackWallProbeY);
		float floorY = std::floor(_pos.Y + AttackFloorProbeY);
		return (_levelHandler->IsPositionEmpty(this, AABBf(px, wallY, px, wallY), params) &&
			!_levelHandler->IsPositionEmpty(this, AABBf(px, floorY, px, floorY), params));
	}

	void Doggy::UpdateSpeed()
	{
		float speed = WalkSpeed;
		if (IsAttacking()) {
			// Faster while the player it's after is ahead of it
			speed = RunSpeed;
			if (auto target = _attackTarget.lock()) {
				float dx = target->GetPos().X - _pos.X;
				if (dx != 0.0f && (dx < 0.0f) == IsFacingLeft()) {
					speed = DashSpeed;
				}
			}
		}
		_speed.X = (IsFacingLeft() ? -speed : speed) * TicksPerFrame;
	}

	void Doggy::ResetSniffTime()
	{
		_noiseCooldown = (30.0f + Random().Next(0, 32)) / TicksPerFrame;
	}

	void Doggy::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteValueAsLE<float>(_attackTime);
		std::uint8_t flags = 0;
		if (_stuck) flags |= 0x01;
		dest.WriteValue<std::uint8_t>(flags);
	}

	void Doggy::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		_attackTime = src.ReadValueAsLE<float>();
		std::uint8_t flags = src.ReadValue<std::uint8_t>();
		_stuck = ((flags & 0x01) != 0);
		SetState(ActorState::ApplyGravitation, !IsAttacking());
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
				// It turns to the shooter and goes after them
				Player* owner = shotBase->GetOwner();
				if (owner != nullptr) {
					SetFacingLeft(owner->GetPos().X < _pos.X);
				}
				StartAttack(owner);
				SetAnimation(AnimState::TransitionAttack);
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
