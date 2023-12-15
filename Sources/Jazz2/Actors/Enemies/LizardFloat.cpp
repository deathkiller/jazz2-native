#include "LizardFloat.h"
#include "Lizard.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Environment/Bomb.h"
#include "../Environment/Copter.h"
#include "../Player.h"
#include "../Solid/PushableBox.h"
#include "../Weapons/TNT.h"

#include "../../../nCine/Base/Random.h"
#include "../../../nCine/Base/FrameTimer.h"

#include <float.h>

namespace Jazz2::Actors::Enemies
{
	LizardFloat::LizardFloat()
		: _attackTime(200.0f), _moveTime(100.0f)
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
		_copterDuration = details.Params[1];
		// TODO: flyAway parameter
		//bool flyAway = (details.Params[2] != 0);

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
			Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() - 2),
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

		bool found = false;
		Vector2f targetPos = Vector2f(FLT_MAX, FLT_MAX);

		auto& players = _levelHandler->GetPlayers();
		for (auto& player : players) {
			Vector2f newPos = player->GetPos();
			if ((_pos - newPos).SqrLength() < (_pos - targetPos).SqrLength()) {
				targetPos = newPos;
				found = true;
			}
		}

		if (found) {
			float distance = (targetPos - _pos).Length();

			if (distance < 280.0f && _attackTime <= 0.0f) {
				SetTransition(AnimState::TransitionAttack, false, [this]() {
					std::shared_ptr<Environment::Bomb> bomb = std::make_shared<Environment::Bomb>();
					uint8_t bombParams[2];
					bombParams[0] = (uint8_t)(_theme + 1);
					bombParams[1] = (IsFacingLeft() ? 1 : 0);
					bomb->OnActivated(ActorActivationDetails(
						_levelHandler,
						Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -30.0f : 30.0f), (std::int32_t)_pos.Y - 10.0f, _renderer.layer() + 2),
						bombParams
					));
					_levelHandler->AddActor(bomb);

					SetTransition(AnimState::TransitionAttackEnd, false);
				});

				_attackTime = Random().NextFloat(120, 240);
			}

			if (distance < 360.0f && _moveTime <= 0.0f) {
				Vector2f diff = (targetPos - _pos).Normalized();

				Vector2f speed = (Vector2f(_speed.X, _speed.Y) + diff * 0.4f).Normalized();
				_speed.X = speed.X * DefaultSpeed;
				_speed.Y = speed.Y * DefaultSpeed;

				SetFacingLeft(_speed.X < 0.0f);

				_moveTime = 8.0f;
			}

			_attackTime -= timeMult;
		}

		_moveTime -= timeMult;
	}

	bool LizardFloat::OnPerish(ActorBase* collider)
	{
		if (_copter != nullptr) {
			if (_copterDuration > 0) {
				_copter->Unmount(_copterDuration * FrameTimer::FramesPerSecond);
			} else {
				_copter->DecreaseHealth(INT32_MAX);
			}
			_copter = nullptr;
		}

		bool shouldDestroy = (_frozenTimeLeft > 0.0f);
		if (auto* player = runtime_cast<Player*>(collider)) {
			if (player->GetSpecialMove() != Player::SpecialMoveType::None) {
				shouldDestroy = true;
			}
		} else if (runtime_cast<Weapons::TNT*>(collider) || runtime_cast<Solid::PushableBox*>(collider)) {
			shouldDestroy = true;
		}

		if (shouldDestroy) {
			CreateDeathDebris(collider);
			_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

			TryGenerateRandomDrop();
		} else {
			std::shared_ptr<Lizard> lizard = std::make_shared<Lizard>();
			uint8_t lizardParams[3];
			lizardParams[0] = _theme;
			lizardParams[1] = 1;
			lizardParams[2] = (IsFacingLeft() ? 1 : 0);
			lizard->OnActivated(ActorActivationDetails(
				_levelHandler,
				Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer()),
				lizardParams
			));
			_levelHandler->AddActor(lizard);

			Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() + 2), Explosion::Type::SmokeGray);
		}

		return EnemyBase::OnPerish(collider);
	}
}