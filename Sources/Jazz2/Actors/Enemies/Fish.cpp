#include "Fish.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Fish::Fish()
		:
		_state(StateIdle),
		_idleTime(0.0f),
		_attackCooldown(60.0f)
	{
	}

	void Fish::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Fish"_s);
	}

	Task<bool> Fish::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::ApplyGravitation, false);

		if (!_levelHandler->IsReforged()) {
			SetState(ActorState::CollideWithTileset, false);
		}

		SetHealthByDifficulty(1);
		_scoreValue = 100;

		async_await RequestMetadataAsync("Enemy/Fish"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Fish::OnUpdate(float timeMult)
	{
		float waterLevel = _levelHandler->WaterLevel();
		bool inWater = (_pos.Y > waterLevel - 160.0f);
		if (!inWater) {
			// Don't move if not in water
			return;
		}

		EnemyBase::OnUpdate(timeMult);

		if (_pos.Y < waterLevel + 8.0f) {
			MoveInstantly(Vector2f(_pos.X, waterLevel + 8.0f), MoveType::Absolute | MoveType::Force);
		}

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		SetState(ActorState::CanJump, false);

		if (_attackCooldown < 0.0f) {
			_attackCooldown = 60.0f;

			Vector2f targetPos;
			auto players = _levelHandler->GetPlayers();
			for (auto* player : players) {
				targetPos = player->GetPos();
				_direction.X = targetPos.X - _pos.X;
				_direction.Y = targetPos.Y - _pos.Y;
				float length = _direction.Length();
				if (length < 320.0f) {
					_direction.Normalize();

					_speed.X = 0.0f;
					_speed.Y = 0.0f;
					SetFacingLeft(_direction.X < 0.0f);
					_state = StateAttacking;

					_attackCooldown = 240.0f;

					SetTransition(AnimState::TransitionAttack, false, [this]() {
						_state = StateBraking;
					});
					break;
				}
			}
		} else {
			if (_state == StateAttacking) {
				_speed.X += _direction.X * 0.11f * timeMult;
				_speed.Y += _direction.Y * 0.11f * timeMult;
			} else if (_state == StateBraking) {
				_speed.X = lerp(_speed.X, _speed.X * 0.96f, timeMult);
				_speed.Y = lerp(_speed.Y, _speed.Y * 0.96f, timeMult);

				if (std::abs(_speed.X) < 0.01f && std::abs(_speed.Y) < 0.01f) {
					_state = StateIdle;
				}
			} else {
				if (_idleTime < 0.0f) {
					float x = Random().NextFloat(-1.4f, 1.4f);
					float y = Random().NextFloat(-2.0f, 2.0f);

					_speed.X = (_speed.X + x) * 0.2f;
					_speed.Y = (_speed.Y + y) * 0.2f;

					_idleTime = 20.0f;
				} else {
					_idleTime -= timeMult;
				}
			}

			_attackCooldown -= timeMult;
		}
	}

	void Fish::OnHitWall(float timeMult)
	{
		EnemyBase::OnHitWall(timeMult);

		_speed.X = 0.0f;
		_speed.Y = 0.0f;

	}

	void Fish::OnHitFloor(float timeMult)
	{
		EnemyBase::OnHitFloor(timeMult);

		_speed.X = 0.0f;
		_speed.Y = 0.0f;
	}

	void Fish::OnHitCeiling(float timeMult)
	{
		EnemyBase::OnHitCeiling(timeMult);

		_speed.X = 0.0f;
		_speed.Y = 0.0f;
	}

	bool Fish::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + _speed.X), (int)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::SmallDark);

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}