#include "Dragonfly.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Dragonfly::Dragonfly()
		:
		_state(StateIdle),
		_idleTime(0.0f),
		_attackCooldown(60.0f)
	{
	}

	Dragonfly::~Dragonfly()
	{
		if (_noise != nullptr) {
			_noise->stop();
			_noise = nullptr;
		}
	}

	void Dragonfly::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Dragonfly"_s);
	}

	Task<bool> Dragonfly::OnActivatedAsync(const ActorActivationDetails& details)
	{
		CollisionFlags &= ~CollisionFlags::ApplyGravitation;

		SetHealthByDifficulty(1);
		_scoreValue = 200;

		co_await RequestMetadataAsync("Enemy/Dragonfly"_s);
		SetAnimation(AnimState::Idle);

		SetFacingLeft(Random().NextBool());

		co_return true;
	}

	void Dragonfly::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0) {
			return;
		}

		SetState(ActorFlags::CanJump, false);

		if (_attackCooldown < 0.0f) {
			_attackCooldown = 40.0f;

			Vector2f targetPos;
			auto& players = _levelHandler->GetPlayers();
			for (auto& player : players) {
				targetPos = player->GetPos();
				_direction.X = targetPos.X - _pos.X;
				_direction.Y = targetPos.Y - _pos.Y;
				float length = _direction.Length();
				if (length < 320.0f && targetPos.Y < _levelHandler->WaterLevel()) {
					_direction.Normalize();

					_speed.X = 0.0f;
					_speed.Y = 0.0f;
					SetFacingLeft(_direction.X < 0.0f);
					_state = StateAttacking;

					_idleTime = Random().NextFloat(40.0f, 60.0f);
					_attackCooldown = Random().NextFloat(130.0f, 200.0f);

					_noise = PlaySfx("Noise"_s, 0.6f);
					break;
				}
			}
		} else {
			if (_state == StateAttacking) {
				if (_idleTime < 0.0f) {
					_state = StateBraking;

					if (_noise != nullptr) {
						// TODO: Fade-out
						//_noise.FadeOut(1f);
						_noise->stop();
						_noise = nullptr;
					}
				} else {
					_idleTime -= timeMult;

					_speed.X += _direction.X * 0.14f * timeMult;
					_speed.Y += _direction.Y * 0.14f * timeMult;
				}
			} else if (_state == StateBraking) {
				float speedMult = std::powf(0.88f, timeMult);
				_speed.X *= speedMult;
				_speed.Y *= speedMult;

				if (std::abs(_speed.X) < 0.01f && std::abs(_speed.Y) < 0.01f) {
					_state = StateIdle;
				}
			} else {
				if (_idleTime < 0.0f) {
					float x = Random().NextFloat(-0.4f, 0.4f);
					float y = Random().NextFloat(-2.0f, 2.0f);

					_speed.X = (_speed.X + x) * 0.2f;
					_speed.Y = (_speed.Y + y) * 0.2f;

					_idleTime = 20.0f;
				} else {
					_idleTime -= timeMult;
				}
			}

			// Can't fly into the water
			if (_pos.Y > _levelHandler->WaterLevel() - 12.0f) {
				_speed.Y = -0.4f;
				_state = StateIdle;

				if (_noise != nullptr) {
					// TODO: Fade-out
					//_noise.FadeOut(0.4f);
					_noise->stop();
					_noise = nullptr;
				}
			}

			_attackCooldown -= timeMult;
		}
	}

	void Dragonfly::OnHitWall()
	{

	}

	void Dragonfly::OnHitFloor()
	{

	}

	void Dragonfly::OnHitCeiling()
	{

	}

	bool Dragonfly::OnPerish(ActorBase* collider)
	{
		if (_noise != nullptr) {
			_noise->stop();
			_noise = nullptr;
		}

		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}