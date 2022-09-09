#include "Bee.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Bee::Bee()
		:
		_anglePhase(0.0f),
		_attackTime(80.0f),
		_attacking(false),
		_returning(false)
	{
	}

	Bee::~Bee()
	{
		if (_noise != nullptr) {
			_noise->stop();
			_noise = nullptr;
		}
	}

	void Bee::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Bee"_s);
	}

	Task<bool> Bee::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		SetHealthByDifficulty(1);
		_scoreValue = 200;

		_originPos = _lastPos = _targetPos = _pos;

		co_await RequestMetadataAsync("Enemy/Bee"_s);
		SetAnimation(AnimState::Idle);

		co_return true;
	}

	void Bee::OnUpdate(float timeMult)
	{
		OnUpdateHitbox();
		HandleBlinking(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			_frozenTimeLeft -= timeMult;
			return;
		}

		if (_attackTime > 0.0f) {
			_attackTime -= timeMult;
		} else {
			if (_attacking) {
				_targetPos = _originPos;

				_attackTime = 90.0f;
				_attacking = false;
				_returning = true;
			} else {
				if (_noise != nullptr) {
					// TODO: Fade-out
					_noise->stop();
					_noise = nullptr;
				}

				AttackNearestPlayer();
			}
		}

		_anglePhase += timeMult * 0.05f;

		Vector2f speed = ((_targetPos - _lastPos) * (_returning ? 0.03f : 0.006f) + _lastSpeed * 1.4f) / 2.4f;
		_lastPos.X += speed.X;
		_lastPos.Y += speed.Y;

		_lastSpeed = speed;

		bool willFaceLeft = (_speed.X < 0.0f);
		if (IsFacingLeft() != willFaceLeft) {
			SetTransition(AnimState::TransitionTurn, false, [this, willFaceLeft]() {
				SetFacingLeft(willFaceLeft);
			});
		}

		MoveInstantly(_lastPos + Vector2f(cosf(_anglePhase) * 16.0f, sinf(_anglePhase) * -16.0f), MoveType::Relative | MoveType::Force);
	}

	bool Bee::OnPerish(ActorBase* collider)
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

	void Bee::AttackNearestPlayer()
	{
		bool found = false;
		auto& players = _levelHandler->GetPlayers();
		for (auto& player : players) {
			Vector2f newPos = player->GetPos();
			if ((_lastPos - newPos).Length() < (_lastPos - _targetPos).Length()) {
				_targetPos = newPos;
				found = true;
			}
		}

		Vector2f diff = (_targetPos - _lastPos);
		if (found && diff.Length() <= 280.0f) {
			_targetPos.X += (_targetPos.X - _originPos.X) * 1.8f;
			_targetPos.Y += (_targetPos.Y - _originPos.Y) * 1.8f;

			// Can't fly into the water
			float waterLevel = _levelHandler->WaterLevel() - 12.0f;
			if (_targetPos.Y > waterLevel) {
				_targetPos.Y = waterLevel;
			}

			_attackTime = 110.0f;
			_attacking = true;
			_returning = false;

			if (_noise == nullptr) {
				_noise = PlaySfx("Noise"_s, 0.5f, 2.0f);
				if (_noise != nullptr) {
					_noise->setLooping(true);
				}
			}
		} else {
			_targetPos = _originPos;
			_returning = true;
		}
	}
}