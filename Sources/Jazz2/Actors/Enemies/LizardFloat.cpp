#include "LizardFloat.h"
#include "Lizard.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventSpawner.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Environment/Bomb.h"
#include "../Environment/Copter.h"
#include "../Player.h"
#include "../Solid/PushableBox.h"
#include "../Weapons/TNT.h"

#include "../../../nCine/Base/Random.h"
#include "../../../nCine/Base/FrameTimer.h"

#include <algorithm>
#include <cmath>
#include <float.h>

namespace Jazz2::Actors::Enemies
{
	LizardFloat::LizardFloat()
		: _attackTime(0.0f), _moveTime(0.0f), _targetIndex(0), _theme(0), _copterDuration(DefaultCopterDuration), _awake(false), _attacking(false),
			_dropsCopter(true)
	{
	}

	void LizardFloat::Preload(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		switch (theme) {
			case 0:
			default:
				PreloadMetadataAsync("Enemy/LizardFloat"_s);
				break;

			case 1: // Xmas
				PreloadMetadataAsync("Enemy/LizardFloatXmas"_s);
				break;
		}
	}

	Task<bool> LizardFloat::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_theme = details.Params[0];
		_copterDuration = (details.Params[1] > 0 ? details.Params[1] : DefaultCopterDuration);
		// The original drops a copter only when the event asks for it
		_dropsCopter = (details.Params[2] != 0);

		SetState(ActorState::ApplyGravitation, false);
		SetState(ActorState::CollideWithTileset, _levelHandler->IsReforged());

		SetHealthByDifficulty(1);
		_scoreValue = 200;

		switch (_theme) {
			case 0:
			default:
				async_await RequestMetadataAsync("Enemy/LizardFloat"_s);
				break;

			case 1: // Xmas
				async_await RequestMetadataAsync("Enemy/LizardFloatXmas"_s);
				break;
		}
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Idle);

		_copter = std::make_shared<Environment::Copter>();
		uint8_t copterParams[1];
		copterParams[0] = 1;
		_copter->OnActivated(ActorActivationDetails(
			_levelHandler,
			Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() - 2),
			copterParams
		));
		_levelHandler->AddActor(_copter);

		async_return true;
	}

	bool LizardFloat::OnTileDeactivated()
	{
		if (_copter != nullptr) {
			_copter->DecreaseHealth(INT32_MAX);
			_copter = nullptr;
		}

		return true;
	}

	void LizardFloat::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_copter != nullptr) {
			_copter->MoveInstantly(Vector2f(_pos.X, _pos.Y + 4.0f), MoveType::Absolute | MoveType::Force);
		}

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		SetState(ActorState::CanJump, false);

		// It hangs still while it throws the bomb
		if (_currentTransition != nullptr) {
			if (!_attacking) {
				_attacking = true;
				_speedBeforeAttack = _speed;
				_speed = Vector2f::Zero;
			}
			return;
		}
		if (_attacking) {
			_attacking = false;
			_speed = _speedBeforeAttack;
		}

		auto players = _levelHandler->GetPlayers();

		Player* nearest = nullptr;
		std::size_t nearestIndex = 0;
		float nearestSqr = FLT_MAX;
		for (std::size_t i = 0; i < players.size(); i++) {
			if (players[i]->GetHealth() <= 0) {
				continue;
			}

			float distanceSqr = (players[i]->GetPos() - _pos).SqrLength();
			if (distanceSqr < nearestSqr) {
				nearestSqr = distanceSqr;
				nearest = players[i];
				nearestIndex = i;
			}
		}

		// It floats in place until a player comes within the wake distance
		if (!_awake) {
			if (nearest == nullptr || nearestSqr >= WakeDistance * WakeDistance) {
				return;
			}
			_awake = true;
		}

		if (_targetIndex >= players.size()) {
			_targetIndex = 0;
		}
		if (players.empty()) {
			return;
		}

		// It keeps a band above the target player and accelerates slowly, the speed limit rises with difficulty
		Vector2f diff = _pos - players[_targetIndex]->GetPos();
		float accelX = (diff.X > KeepDistanceX ? -Acceleration : (diff.X < -KeepDistanceX ? Acceleration : 0.0f));
		float accelY = (diff.Y > 0.0f ? -Acceleration : (diff.Y < -HoverHeight ? Acceleration : 0.0f));
		float keep = std::pow(SpeedKeptPerTick, timeMult * TicksPerFrame);
		_speed.X = (accelX != 0.0f ? _speed.X + accelX * timeMult : _speed.X * keep);
		_speed.Y = (accelY != 0.0f ? _speed.Y + accelY * timeMult : _speed.Y * keep);

		std::int32_t difficultyLevel = 1;
		switch (_levelHandler->GetDifficulty()) {
			case GameDifficulty::Easy: difficultyLevel = 0; break;
			case GameDifficulty::Hard: difficultyLevel = 2; break;
			default: break;
		}
		float maxSpeed = (2.0f + 2.0f * difficultyLevel) * TicksPerFrame;
		_speed.X = std::clamp(_speed.X, -maxSpeed, maxSpeed);
		_speed.Y = std::clamp(_speed.Y, -maxSpeed, maxSpeed);

		// It faces the nearest player only while that player is close, and attacks after being near for a while
		if (nearest != nullptr && nearestSqr < AttackDistance * AttackDistance) {
			if (nearest->GetPos().X != _pos.X) {
				SetFacingLeft(nearest->GetPos().X < _pos.X);
			}

			_attackTime += timeMult;
			if (_attackTime > AttackWaitTicks / TicksPerFrame) {
				_attackTime = 0.0f;
				_targetIndex = static_cast<std::uint8_t>(nearestIndex);

				SetTransition(AnimState::TransitionAttack, false, [this]() {
					std::shared_ptr<Environment::Bomb> bomb = std::make_shared<Environment::Bomb>();
					uint8_t bombParams[2];
					bombParams[0] = (uint8_t)(_theme + 1);
					bombParams[1] = (IsFacingLeft() ? 1 : 0);
					bomb->OnActivated(ActorActivationDetails(
						_levelHandler,
						Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -30 : 30), (std::int32_t)_pos.Y - 10, _renderer.GetLayer() + 2),
						bombParams
					));
					_levelHandler->AddActor(bomb);

					SetTransition(AnimState::TransitionAttackEnd, false);
				});

				_attacking = true;
				_speedBeforeAttack = _speed;
				_speed = Vector2f::Zero;
			}
		}
	}

	void LizardFloat::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteValueAsLE<float>(_attackTime);
		dest.WriteValueAsLE<float>(_moveTime);
	}

	void LizardFloat::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		_attackTime = src.ReadValueAsLE<float>();
		_moveTime = src.ReadValueAsLE<float>();
		_awake = true;

		// The copter was created again at the spawn position
		if (_copter != nullptr) {
			_copter->MoveInstantly(Vector2f(_pos.X, _pos.Y + 4.0f), MoveType::Absolute | MoveType::Force);
		}
	}

	bool LizardFloat::OnPerish(ActorBase* collider)
	{
		if (_copter != nullptr) {
			if (_dropsCopter) {
				_copter->Unmount(_copterDuration * FrameTimer::FramesPerSecond);
			} else {
				_copter->DecreaseHealth(INT32_MAX);
			}
			_copter = nullptr;
		}

		bool shouldDestroy = (_frozenTimeLeft > 0.0f);
		if (auto* player = runtime_cast<Player>(collider)) {
			if (player->GetSpecialMove() != Player::SpecialMoveType::None) {
				shouldDestroy = true;
			}
		} else if (runtime_cast<Weapons::TNT>(collider) || runtime_cast<Solid::PushableBox>(collider)) {
			shouldDestroy = true;
		}

		if (shouldDestroy) {
			CreateParticleDebrisOnPerish(collider);
			_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

			TryGenerateRandomDrop();
		} else {
			std::shared_ptr<Lizard> lizard = std::make_shared<Lizard>();
			std::uint8_t lizardParams[Events::EventSpawner::SpawnParamsSize] = {};
			lizardParams[0] = _theme;
			lizardParams[1] = 1;
			lizardParams[2] = (IsFacingLeft() ? 1 : 0);
			ActorActivationDetails details(
				_levelHandler,
				Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer()),
				lizardParams
			);
			// The lizard is a regular event object, so it can be spawned again from a level state snapshot
			details.Type = EventType::EnemyLizard;
			lizard->OnActivated(details);
			_levelHandler->AddActor(lizard);

			Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() + 2), Explosion::Type::SmokeGray);
		}

		return EnemyBase::OnPerish(collider);
	}
}