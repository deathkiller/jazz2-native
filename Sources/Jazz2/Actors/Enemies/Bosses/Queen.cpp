#include "Queen.h"
#include "../../../ILevelHandler.h"
#include "../../Player.h"
#include "../../Environment/Spring.h"

#include "../../../../nCine/Base/Random.h"

#include <float.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Bosses
{
	Queen::Queen()
		:
		_state(StateWaiting),
		_stateTime(0.0f),
		_endText(0),
		_queuedBackstep(false)
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
		SetState(ActorState::IsInvulnerable | ActorState::IsSolidObject | ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::CanBeFrozen, false);
		_health = INT32_MAX;
		_maxHealth = INT32_MAX;
		_scoreValue = 0;

		_lastHealth = _health;

		_stepSize = 0.3f;
		switch (_levelHandler->Difficulty()) {
			case GameDifficulty::Easy: _stepSize *= 1.3f; break;
			case GameDifficulty::Hard: _stepSize *= 0.7f; break;
		}

		async_await RequestMetadataAsync("Boss/Queen"_s);
		SetAnimation(AnimState::Idle);

		// Invisible block above the queen
		_block = std::make_shared<InvisibleBlock>();
		_block->OnActivated(ActorActivationDetails(
			_levelHandler,
			Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() + 1)
		));
		_levelHandler->AddActor(_block);

		async_return true;
	}

	bool Queen::OnActivatedBoss()
	{
		MoveInstantly(_originPos, MoveType::Absolute | MoveType::Force);
		_state = StateWaiting;
		return true;
	}

	void Queen::OnUpdate(float timeMult)
	{
		TileCollisionParams params = { TileDestructType::Weapon | TileDestructType::Speed, _speed.Y >= 0.0f, WeaponType::Blaster, 1 };
		TryStandardMovement(timeMult, params);
		OnUpdateHitbox();
		if (params.TilesDestroyed > 0) {
			_levelHandler->ShakeCameraView(20.0f);
		}

		HandleBlinking(timeMult);

		if (_block != nullptr) {
			_block->MoveInstantly(Vector2f(_pos.X, _pos.Y - 32.0f), MoveType::Absolute | MoveType::Force);
		}

		switch (_state) {
			case StateWaiting: {
				// Waiting for player to enter the arena
				_levelHandler->FindCollisionActorsByAABB(this, AABBf(_pos.X - 300, _pos.Y - 120, _pos.X + 60, _pos.Y + 120), [this](ActorBase* actor) {
					if (auto* player = runtime_cast<Player*>(actor)) {
						_state = StateIdleToScream;
						_stateTime = 260.0f;
						return false;
					}
					return true;
				});
				break;
			}

			case StateIdleToScream: {
				// Scream towards the player
				if (_stateTime <= 0.0f) {
					_lastHealth = _health;
					SetState(ActorState::IsInvulnerable, false);

					_state = StateScreaming;
					PlaySfx("Scream"_s);
					SetTransition((AnimState)1073741824, false, [this]() {
						_state = (Random().NextFloat() < 0.8f ? StateIdleToStomp : StateIdleToBackstep);
						_stateTime = Random().NextFloat(65.0f, 85.0f);

						SetState(ActorState::IsInvulnerable, true);

						if (_lastHealth - _health >= 2) {
							_queuedBackstep = true;
						}
					});
				}
				break;
			}

			case StateIdleToStomp: {
				if (_stateTime <= 0.0f) {
					_state = StateTransition;
					SetTransition((AnimState)1073741825, false, [this]() {
						PlaySfx("Stomp"_s);

						SetTransition((AnimState)1073741830, false, [this]() {
							_state = StateIdleToBackstep;
							_stateTime = Random().NextFloat(70.0f, 80.0f);

							auto& players = _levelHandler->GetPlayers();
							auto player = players[Random().Next(0, (std::uint32_t)players.size())];

							std::shared_ptr<Brick> brick = std::make_shared<Brick>();
							brick->OnActivated(ActorActivationDetails(
								_levelHandler,
								Vector3i((std::int32_t)(player->GetPos().X + Random().NextFloat(-50.0f, 50.0f)), (std::int32_t)(_pos.Y - 200.0f), _renderer.layer() - 20)
							));
							_levelHandler->AddActor(brick);

							_levelHandler->ShakeCameraView(20.0f);
						});
					});
				}
				break;
			}

			case StateIdleToBackstep: {
				if (_stateTime <= 0.0f) {
					if (_queuedBackstep) {
						_queuedBackstep = false;

						// 2 hits by player while screaming, step backwards
						_speed.X = _stepSize;

						_state = StateTransition;
						SetTransition((AnimState)1073741826, false, [this]() {
							_speed.X = 0.0f;

							// Check it it's on the ledge
							AABBf aabb1 = AABBf(_pos.X - 10, _pos.Y + 24, _pos.X - 6, _pos.Y + 28);
							AABBf aabb2 = AABBf(_pos.X + 6, _pos.Y + 24, _pos.X + 10, _pos.Y + 28);
							TileCollisionParams params = { TileDestructType::None, true };
							if (!_levelHandler->IsPositionEmpty(this, aabb1, params) && _levelHandler->IsPositionEmpty(this, aabb2, params)) {
								_lastHealth = _health;
								SetState(ActorState::IsInvulnerable, false);

								// It's on the ledge
								_state = StateTransition;
								SetTransition((AnimState)1073741827, false, [this]() {
									SetState(ActorState::IsInvulnerable, true);

									// 1 hits by player
									if (_lastHealth - _health >= 1) {
										// Fall off the ledge
										_speed.X = 2.2f;

										SetAnimation(AnimState::Fall);
									} else {
										// Recover, step forward
										SetTransition((AnimState)1073741828, false, [this]() {
											_speed.X = _stepSize * -1.3f;

											SetTransition((AnimState)1073741826, false, [this]() {
												_speed.X = 0.0f;

												_state = StateIdleToScream;
												_stateTime = Random().NextFloat(150.0f, 180.0f);
											});
										});
									}
								});
							} else {
								_state = StateIdleToScream;
								_stateTime = Random().NextFloat(160.0f, 200.0f);
							}
						});
					} else {
						_state = StateIdleToScream;
						_stateTime = Random().NextFloat(150.0f, 180.0f);
					}
				}
				break;
			}

			case StateDead: {
				// Thrown away by spring
				if (_stateTime <= 0.0f) {
					DecreaseHealth(INT32_MAX);
				}
				break;
			}

			case StateScreaming: {
				auto& players = _levelHandler->GetPlayers();
				for (auto player : players) {
					//player->AddExternalForce(-1.5f * timeMult, 0.0f);
					player->MoveInstantly(Vector2f(-1.5f * timeMult, 0.0f), MoveType::Relative);
				}
				break;
			}
		}

		_stateTime -= timeMult;
	}

	bool Queen::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* spring = runtime_cast<Environment::Spring*>(other)) {
			// Collide only with hitbox
			if (AABBInner.Overlaps(spring->AABBInner)) {
				Vector2f force = spring->Activate();
				int sign = ((force.X + force.Y) > 0.0f ? 1 : -1);
				if (std::abs(force.X) > 0.0f) {
					_speed.X = (4 + std::abs(force.X)) * sign;
					_externalForce.X = force.X;
				} else if (std::abs(force.Y) > 0.0f) {
					_speed.Y = (4.0f + std::abs(force.Y)) * sign;
					_externalForce.Y = force.Y;
				} else {
					return BossBase::OnHandleCollision(other);
				}
				SetState(ActorState::CanJump, false);

				SetAnimation(AnimState::Fall);
				PlaySfx("Spring"_s);

				if (_state != StateDead) {
					StringView text = _levelHandler->GetLevelText(_endText);
					_levelHandler->ShowLevelText(text);

					_state = StateDead;
					_stateTime = 50.0f;
				}
			}
		}

		return BossBase::OnHandleCollision(other);
	}

	Task<bool> Queen::Brick::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_timeLeft = 50.0f;

		SetState(ActorState::IsInvulnerable | ActorState::SkipPerPixelCollisions, true);
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
			CreateDeathDebris(collider);
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

		_renderer.setDrawEnabled(false);

		async_return true;
	}

	void Queen::InvisibleBlock::OnUpdate(float timeMult)
	{
		// Nothing to update
	}
}