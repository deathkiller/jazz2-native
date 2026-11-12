#include "Turtle.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventSpawner.h"
#include "../../Tiles/TileMap.h"
#include "TurtleShell.h"
#include "../Explosion.h"
#include "../Player.h"
#include "../Solid/PushableBox.h"
#include "../Weapons/ShieldFireShot.h"
#include "../Weapons/Thunderbolt.h"
#include "../Weapons/ToasterShot.h"

#include "../../../nCine/Base/Random.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Enemies
{
	static constexpr AnimState WithdrawStart = (AnimState)20;
	static constexpr AnimState WithdrawStartFast = (AnimState)21;
	static constexpr AnimState WithdrawInProgress = (AnimState)22;
	static constexpr AnimState WithdrawEnd = (AnimState)23;

	Turtle::Turtle()
		: _isAttacking(false), _isTurning(false), _isWithdrawn(false), _isDodging(false), _dodgeCooldown(0.0f), _walkTime(0.0f)
	{
	}

	void Turtle::Preload(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		switch (theme) {
			case 0:
			default:
				PreloadMetadataAsync("Enemy/Turtle"_s);
				PreloadMetadataAsync("Enemy/TurtleShell"_s);
				break;
			case 1: // Xmas
				PreloadMetadataAsync("Enemy/TurtleXmas"_s);
				PreloadMetadataAsync("Enemy/TurtleShellXmas"_s);
				break;
		}
	}

	Task<bool> Turtle::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(1);
		_scoreValue = 100;

		SetState(ActorState::CollideWithTilesetReduced, true);

		_theme = details.Params[0];
		switch (_theme) {
			case 0:
			default:
				async_await RequestMetadataAsync("Enemy/Turtle"_s);
				break;
			case 1: // Xmas
				async_await RequestMetadataAsync("Enemy/TurtleXmas"_s);
				break;
		}
		SetFacingLeft(nCine::Random().NextBool());
		SetAnimation(AnimState::Walk);

		_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;

		PlaceOnGround();

		async_return true;
	}

	void Turtle::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (GetState(ActorState::CanJump) && _currentTransition == nullptr) {
			if (_isDodging) {
				// It stays in its shell while a player is close
				if (!HasPlayerWithin(WithdrawHoldDistance)) {
					_isDodging = false;
					SetAnimation(AnimState::Walk);
					SetTransition(WithdrawEnd, false, [this]() {
						_canHurtPlayer = true;
						_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;
						_walkTime = 0.0f;
					});
					if (_currentTransition != nullptr) {
						SetAnimationTicks(_currentTransition->FrameCount * WithdrawFrameTicks);
					}
					PlaySfx("WithdrawEnd"_s, 0.2f);
				}
			} else if (!_isTurning && !_isWithdrawn && !_isAttacking) {
				_walkTime += timeMult;
				float distanceSqr;
				Player* target = (_walkTime > ReadyTicks / TicksPerFrame ? FindFacedPlayer(distanceSqr) : nullptr);
				if (target != nullptr) {
					if (distanceSqr < BiteDistance * BiteDistance) {
						Attack();
					} else if (distanceSqr > WithdrawDistance * WithdrawDistance && target->HasFiredWithin(FireWindowTicks / TicksPerFrame)) {
						_isDodging = true;
						_canHurtPlayer = false;
						_speed.X = 0;
						SetAnimation(WithdrawInProgress);
						SetTransition(WithdrawStartFast, false);
						if (_currentTransition != nullptr) {
							SetAnimationTicks(_currentTransition->FrameCount * WithdrawFrameTicks);
						}
						PlaySfx("Withdraw"_s, 0.2f);
					}
				}
			}

			if (std::abs(_speed.X) > 0.0f && !CanMoveToPosition(_speed.X * 4, 0)) {
				_isTurning = true;
				_canHurtPlayer = false;
				_speed.X = 0;
				// Measured in JJ2+: the duck goes on from the frame the walk was at, so a late frame ducks quicker
				std::int32_t walkFrame = _renderer.CurrentFrame - _renderer.FirstFrame;
				SetTransition(WithdrawStart, false, [this]() {
					HandleTurn(true);
				});
				if (_renderer.FrameCount > 0) {
					std::int32_t startFrame = std::clamp<std::int32_t>(walkFrame, 0, _renderer.FrameCount - 1);
					SetAnimationTicks(_renderer.FrameCount * TurnFrameTicks);
					SetAnimationTimeTicks(startFrame * TurnFrameTicks + TurnFrameElapsedTicks);
				}
				PlaySfx("Withdraw"_s, 0.2f);
			}
		}

		if (_isAttacking) {
			// Turtles attack only with animation, so check collisions every frame
			SetState(ActorState::IsDirty, true);
		}

		// Measured in JJ2+: it walks at 8 ticks a frame, the frame it reaches a wall at decides how much of the duck shows
		if (_currentTransition == nullptr && !_isDodging && _renderer.FrameCount > 0) {
			ChangeAnimationTicks(_renderer.FrameCount * WalkFrameTicks);
		}
	}

	void Turtle::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		std::uint8_t flags = 0;
		if (_isAttacking) flags |= 0x01;
		if (_isTurning) flags |= 0x02;
		if (_isWithdrawn) flags |= 0x04;
		if (_isDodging) flags |= 0x08;
		if (_currentTransition != nullptr) flags |= 0x10;
		dest.WriteValue<std::uint8_t>(flags);
		dest.WriteValueAsLE<float>(_dodgeCooldown);
	}

	void Turtle::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		std::uint8_t flags = src.ReadValue<std::uint8_t>();
		_isAttacking = ((flags & 0x01) != 0);
		_isTurning = ((flags & 0x02) != 0);
		_isWithdrawn = ((flags & 0x04) != 0);
		_isDodging = ((flags & 0x08) != 0);
		bool inTransition = ((flags & 0x10) != 0);
		_dodgeCooldown = src.ReadValueAsLE<float>();

		// Transition callbacks cannot be restored, so finish the interrupted action as they would
		if (_isAttacking) {
			_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;
			_isAttacking = false;
		} else if (_isTurning) {
			if (!_isWithdrawn) {
				SetFacingLeft(!IsFacingLeft());
			}
			_canHurtPlayer = true;
			_isWithdrawn = false;
			_isTurning = false;
			_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;
		} else if (!_isDodging && inTransition) {
			// Coming out of the shell after dodging
			_canHurtPlayer = true;
			_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;
		}
	}

	void Turtle::OnUpdateHitbox()
	{
		UpdateHitbox(24, 24);
	}

	bool Turtle::OnPerish(ActorBase* collider)
	{
		// Animation should be paused only if enemy is frozen
		bool shouldDestroy = (_frozenTimeLeft > 0.0f);
		if (auto* player = runtime_cast<Player>(collider)) {
			if (player->GetSpecialMove() != Player::SpecialMoveType::None) {
				shouldDestroy = true;
			}
		} else if (runtime_cast<Weapons::ToasterShot>(collider) || runtime_cast<Weapons::ShieldFireShot>(collider) ||
				   runtime_cast<Weapons::Thunderbolt>(collider) || runtime_cast<Solid::PushableBox>(collider)) {
			shouldDestroy = true;
		}

		if (shouldDestroy) {
			CreateParticleDebrisOnPerish(collider);
			_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

			// Add score also for turtle shell
			_scoreValue += 100;

			TryGenerateRandomDrop();
		} else {
			float shellSpeedY = (_pos.Y > _levelHandler->GetWaterLevel() ? -0.65f : -0.98f);

			std::shared_ptr<TurtleShell> shell = std::make_shared<TurtleShell>();
			std::uint8_t shellParams[Events::EventSpawner::SpawnParamsSize] = {};
			EventParamsWriter writer(shellParams);
			writer.SetFloat(0, _speed.X * 1.1f);
			writer.SetFloat(4, shellSpeedY);
			writer.SetUint8(8, _theme);
			ActorActivationDetails details(
				_levelHandler,
				Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer()),
				shellParams
			);
			// The shell is a regular event object, so it can be spawned again from a level state snapshot
			details.Type = EventType::TurtleShell;
			shell->OnActivated(details);
			_levelHandler->AddActor(shell);

			Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() - 2), Explosion::Type::SmokeGray);
		}

		return EnemyBase::OnPerish(collider);
	}

	void Turtle::HandleTurn(bool isFirstPhase)
	{
		if (_isTurning) {
			if (isFirstPhase) {
				SetFacingLeft(!IsFacingLeft());
				SetTransition(WithdrawEnd, false, [this]() {
				   HandleTurn(false);
				});
				SetAnimationTicks(TurnEndTicks);
				PlaySfx("WithdrawEnd"_s, 0.2f);
				_isWithdrawn = true;
			} else {
				_canHurtPlayer = true;
				_isWithdrawn = false;
				_isTurning = false;
				_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;
				_walkTime = 0.0f;
			}
		}
	}

	void Turtle::Attack()
	{
		_speed.X = 0;
		_isAttacking = true;
		PlaySfx("Attack"_s);

		SetTransition(AnimState::TransitionAttack, false, [this]() {
			_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;
			_isAttacking = false;
			_walkTime = 0.0f;

			// TODO: Bad timing
			PlaySfx("Attack2"_s);
		});
	}

	Player* Turtle::FindFacedPlayer(float& distanceSqr) const
	{
		Player* found = nullptr;
		float nearestSqr = SightDistance * SightDistance;
		for (auto* player : _levelHandler->GetPlayers()) {
			if (player->GetHealth() <= 0) {
				continue;
			}

			// The original checks the height only for a turtle facing right, JJ2+ checks it either way
			Vector2f diff = player->GetPos() - _pos;
			bool inFront = ((IsFacingLeft() ? diff.X < 0.0f : diff.X > 0.0f) && std::abs(diff.Y) < SightHeight);
			float sqr = diff.SqrLength();
			if (inFront && sqr < nearestSqr) {
				nearestSqr = sqr;
				found = player;
			}
		}
		distanceSqr = nearestSqr;
		return found;
	}

	bool Turtle::OnHandleCollision(ActorBase* other)
	{
		// Shots bounce off the shell while it turns, and once it has fully withdrawn from a shot (measured in JJ2+)
		bool inShell = (_isTurning || (_isDodging && _currentTransition == nullptr));
		if (inShell) {
			if (auto* shotBase = runtime_cast<Weapons::ShotBase>(other)) {
				shotBase->TriggerRicochet(this);
				return true;
			}
		}

		return EnemyBase::OnHandleCollision(other);
	}

	bool Turtle::HasPlayerWithin(float distance) const
	{
		return (FindNearestPlayer(distance * distance) != nullptr);
	}
}