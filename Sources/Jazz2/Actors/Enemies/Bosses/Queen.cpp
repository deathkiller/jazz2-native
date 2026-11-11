#include "Queen.h"
#include "../../../ILevelHandler.h"
#include "../../Player.h"
#include "../../Environment/Spring.h"
#include "../../Weapons/ShotBase.h"

#include "../../../../nCine/Base/Random.h"

#include <cmath>

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Bosses
{
	Queen::Queen()
		: _state(StateWaiting), _ticks(0.0f), _idleTicks(FirstIdleTicks), _hits(0), _backstepLoops(0), _endText(0)
	{
	}

	Queen::~Queen()
	{
		if (_block != nullptr) {
			_block->SetState(ActorState::IsDestroyed, true);
			_block = nullptr;
		}
	}

	void Queen::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Boss/Queen"_s);
	}

	Task<bool> Queen::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_endText = details.Params[1];
		_pos.Y -= 8.0f;
		_originPos = _pos;

		_canHurtPlayer = false;
		SetState(ActorState::IsSolidObject | ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::CanBeFrozen, false);
		SetShielded(true);
		_health = INT32_MAX;
		_maxHealth = INT32_MAX;
		_scoreValue = 0;

		async_await RequestMetadataAsync("Boss/Queen"_s);
		SetAnimation(AnimState::Idle);

		// Invisible block above the queen
		_block = std::make_shared<InvisibleBlock>();
		_block->OnActivated(ActorActivationDetails(
			_levelHandler,
			Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() + 1)
		));
		_levelHandler->AddActor(_block);

		async_return true;
	}

	bool Queen::OnActivatedBoss()
	{
		MoveInstantly(_originPos, MoveType::Absolute | MoveType::Force);
		_hits = 0;
		EnterIdle(FirstIdleTicks);
		return true;
	}

	void Queen::OnUpdate(float timeMult)
	{
		TileCollisionParams params = { TileDestructType::Weapon | TileDestructType::Speed, _speed.Y >= 0.0f, WeaponType::Blaster, 1 };
		// Speed tiles break for whoever declares a speed, so she has to
		params.Speed = 64.0f;
		TryStandardMovement(timeMult, params);
		OnUpdateHitbox();
		if (params.TilesDestroyed > 0) {
			_levelHandler->ShakeCameraViewNear(_pos, 20.0f);
		}

		HandleBlinking(timeMult);

		if (_block != nullptr) {
			_block->MoveInstantly(Vector2f(_pos.X, _pos.Y - 32.0f), MoveType::Absolute | MoveType::Force);
		}

		if (_state != StateWaiting) {
			float ticks = timeMult * TicksPerFrame;

			switch (_state) {
				case StateIdle: {
					if (IsPlayerInRegion()) {
						_idleTicks -= ticks;
						if (_idleTicks < 0.0f || _hits >= HitsForBackstep) {
							ChooseAction();
						}
					}
					break;
				}
				case StateScream: {
					_ticks += ticks;

					// Every player is pushed away, harder on Hard
					float push = (GetDifficultyIndex() == 2 ? 2.0f : 1.0f) * TicksPerFrame * timeMult;
					auto players = _levelHandler->GetPlayers();
					for (auto* player : players) {
						if (player->GetHealth() <= 0) {
							continue;
						}

						player->MoveInstantly(Vector2f(-push, 0.0f), MoveType::Relative);
						// Resync the push to the owning client, otherwise remote players aren't affected in multiplayer
						_levelHandler->HandlePlayerPushed(player);
					}

					if (_ticks >= ScreamTicks) {
						EnterIdle(ShortIdleTicks);
					}
					break;
				}
				case StateStomp: {
					_ticks += ticks;
					if (_ticks >= StompTicks) {
						Player* player = FindNearestPlayer(StompRangeSqr);
						if (player != nullptr) {
							std::shared_ptr<Brick> brick = std::make_shared<Brick>();
							brick->OnActivated(ActorActivationDetails(
								_levelHandler,
								Vector3i((std::int32_t)player->GetPos().X, (std::int32_t)(_originPos.Y - BrickHeight), _renderer.GetLayer() - 20)
							));
							_levelHandler->AddActor(Death::move(brick));
						}

						_levelHandler->ShakeCameraViewNear(_pos, 20.0f);
						EnterIdle(LongIdleTicks);
					}
					break;
				}
				case StateBackstep: {
					_ticks += ticks;
					if (_backstepLoops == 2) {
						// The step is spread over the first loop, so she doesn't jump back at its end
						MoveInstantly(Vector2f(BackstepDistance * ticks / BackstepLoopTicks, 0.0f), MoveType::Relative | MoveType::Force);
					}
					if (_ticks >= BackstepLoopTicks) {
						_ticks -= BackstepLoopTicks;
						_backstepLoops--;
						if (_backstepLoops < 1) {
							EnterIdle(ShortIdleTicks);
						} else {
							// After the step it checks for ground under her
							float oldX = _pos.X - BackstepDistance;
							if (!IsSolidAt(oldX + 18.0f, _pos.Y)) {
								_state = StateLedge;
								_ticks = 0.0f;
								SetShielded(false);
								SetTransition((AnimState)1073741827, false);
							} else {
								SetTransition((AnimState)1073741826, false);
							}
						}
					}
					break;
				}
				case StateLedge: {
					_ticks += ticks;
					// The fall starts once the ledge animation has played
					if (_ticks >= LedgeTicks || _currentTransition == nullptr) {
						_state = StateFalling;
						SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset, false);
						MoveInstantly(Vector2f(2.0f, 0.0f), MoveType::Relative | MoveType::Force);
						_speed.X = FallSpeedX;
						_speed.Y = 0.0f;
						SetAnimation(AnimState::Fall);
					}
					break;
				}
				case StateFalling: {
					_speed.X *= std::pow(15.0f / 16.0f, ticks);
					_speed.Y += Gravity * timeMult;

					if (FindSpring()) {
						_state = StateFlung;
						_ticks = 0.0f;
						_speed.X = 0.0f;
						_speed.Y = -LaunchSpeed;
						SetState(ActorState::CollideWithTileset, true);
						SetAnimation(AnimState::Fall);
						PlaySfx("Spring"_s);
					}
					break;
				}
				case StateFlung: {
					_ticks += ticks;
					_speed.Y = -LaunchSpeed;

					if (_ticks >= FlightTicks) {
						StringView text = _levelHandler->GetLevelText(_endText);
						_levelHandler->ShowLevelText(text);
						DecreaseHealth(INT32_MAX);
					}
					break;
				}
			}

			// The players cannot get past a wall in front of her
			if (_state != StateWaiting) {
				auto players = _levelHandler->GetPlayers();
				for (auto* player : players) {
					float limit = _pos.X - WallDistance;
					if (player->GetHealth() > 0 && player->GetPos().X > limit) {
						player->MoveInstantly(Vector2f(limit - player->GetPos().X, 0.0f), MoveType::Relative);
					}
				}
			}
		}
	}

	bool Queen::OnHandleCollision(ActorBase* other)
	{
		if (auto* shotBase = runtime_cast<Weapons::ShotBase>(other)) {
			if (GetState(ActorState::IsInvulnerable)) {
				WeaponType weaponType = shotBase->GetWeaponType();
				if (weaponType != WeaponType::Electro && weaponType != WeaponType::Thunderbolt) {
					shotBase->TriggerRicochet(this);
				}
				return true;
			}

			// Hits only count while she is not shielded
			if (_state != StateWaiting) {
				_hits++;
			}
		}

		return BossBase::OnHandleCollision(other);
	}

	void Queen::SetShielded(bool value)
	{
		SetState(ActorState::IsInvulnerable, value);
		// Shots must not destroy themselves on contact, so the ricochet doesn't depend on collision order
		CanCollideWithShots = !value;
	}

	bool Queen::IsPlayerInRegion()
	{
		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			if (player->GetHealth() > 0 && player->GetPos().X > _originPos.X - RegionLeft && player->GetPos().Y > _originPos.Y - RegionTop) {
				return true;
			}
		}
		return false;
	}

	void Queen::ChooseAction()
	{
		// The hits are spent on whichever action follows, otherwise she acts again the moment she is idle
		std::int32_t hits = _hits;
		_hits = 0;
		if (hits < HitsForBackstep) {
			if (Random().Next(0, 2) == 0) {
				StartScream();
			} else {
				StartStomp();
			}
		} else {
			std::int32_t r = Random().Next(0, 8);
			if (r < 5) {
				StartBackstep();
			} else if (r < 7) {
				StartScream();
			} else {
				StartStomp();
			}
		}
	}

	void Queen::StartScream()
	{
		_state = StateScream;
		_ticks = 0.0f;
		SetShielded(false);
		PlaySfx("Scream"_s);
		SetTransition((AnimState)1073741824, false);
	}

	void Queen::StartStomp()
	{
		_state = StateStomp;
		_ticks = 0.0f;
		SetShielded(false);
		PlaySfx("Stomp"_s);
		SetTransition((AnimState)1073741825, false);
	}

	void Queen::StartBackstep()
	{
		_state = StateBackstep;
		_ticks = 0.0f;
		_hits = 0;
		_backstepLoops = 2;
		SetShielded(false);
		SetTransition((AnimState)1073741826, false);
	}

	void Queen::EnterIdle(float idleTicks)
	{
		_state = StateIdle;
		_ticks = 0.0f;
		_idleTicks = idleTicks;
		_speed.X = 0.0f;
		SetShielded(true);
		SetAnimation(AnimState::Idle);
	}

	bool Queen::IsSolidAt(float x, float y)
	{
		TileCollisionParams params = { TileDestructType::None, true };
		// A column from her middle to below her feet, so the height of her hitbox doesn't matter
		AABBf aabb = AABBf(x - 1.0f, y, x + 1.0f, y + LedgeProbeDepth);
		return !_levelHandler->IsPositionEmpty(this, aabb, params);
	}

	bool Queen::FindSpring()
	{
		bool found = false;
		_levelHandler->FindCollisionActorsByAABB(this, AABBf(_pos.X - SpringRange, _pos.Y - SpringRange, _pos.X + SpringRange, _pos.Y + SpringRange), [this, &found](ActorBase* actor) {
			if (auto* spring = runtime_cast<Environment::Spring>(actor)) {
				if (spring->GetPos().Y < _pos.Y) {
					found = true;
					return false;
				}
			}
			return true;
		});
		return found;
	}

	Task<bool> Queen::Brick::OnActivatedAsync(const ActorActivationDetails& details)
	{
		// Long enough to fall out of the view
		_timeLeft = 180.0f;

		SetState(ActorState::IsInvulnerable | ActorState::SkipPerPixelCollisions, true);
		// It falls through the level, otherwise it gets stuck in the ceiling it starts in
		SetState(ActorState::CollideWithTileset, false);

		async_await RequestMetadataAsync("Boss/Queen"_s);
		SetAnimation((AnimState)1073741829);

		PlaySfx("BrickFalling"_s, 0.3f);

		async_return true;
	}

	void Queen::Brick::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_timeLeft <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		} else {
			_timeLeft -= timeMult;
		}
	}

	bool Queen::Brick::OnPerish(ActorBase* collider)
	{
		if (collider != nullptr) {
			CreateParticleDebrisOnPerish(collider);
		}

		return EnemyBase::OnPerish(collider);
	}

	Task<bool> Queen::InvisibleBlock::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::IsSolidObject | ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::CollideWithTileset, false);

		_health = INT32_MAX;

		async_await RequestMetadataAsync("Boss/Queen"_s);
		SetAnimation(AnimState::Idle);

		_renderer.SetDrawEnabled(false);

		async_return true;
	}

	void Queen::InvisibleBlock::OnUpdate(float timeMult)
	{
		// Nothing to update
	}
}
