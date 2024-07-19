#include "Bee.h"
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

		async_await RequestMetadataAsync("Enemy/Bee"_s);
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Bee::OnUpdate(float timeMult)
	{
		OnUpdateHitbox();
		HandleBlinking(timeMult);
		UpdateFrozenState(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			if (_noise != nullptr) {
				_noise->pause();
			}
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
		if (_noise != nullptr) {
			_noise->play();
		}

		_anglePhase += timeMult * 0.05f;

		Vector2f speed = ((_targetPos - _lastPos) * (_returning ? 0.01f : 0.002f) + _lastSpeed * 1.4f) / 2.4f;
		_lastPos.X += speed.X;
		_lastPos.Y += speed.Y;
		_lastSpeed = speed;

		bool willFaceLeft = (speed.X < 0.0f);
		if (IsFacingLeft() != willFaceLeft) {
			SetTransition(AnimState::TransitionTurn, false, [this, willFaceLeft]() {
				SetFacingLeft(willFaceLeft);
			});
		}

		MoveInstantly(_lastPos + Vector2f(cosf(_anglePhase) * 16.0f, sinf(_anglePhase) * -16.0f), MoveType::Absolute | MoveType::Force);
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
		float targetDistance = 300.0f;
		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			Vector2f newPos = player->GetPos();
			float distance = (_lastPos - newPos).Length();
			if (distance < targetDistance) {
				_targetPos = newPos;
				targetDistance = distance;
				found = true;
			}
		}

		if (found) {
			_targetPos.X += (_targetPos.X - _originPos.X) * 1.6f;
			_targetPos.Y += (_targetPos.Y - _originPos.Y) * 1.6f;

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