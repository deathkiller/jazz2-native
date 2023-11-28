#include "Raven.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Base/Algorithms.h"
#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Raven::Raven()
		:
		_anglePhase(0.0f),
		_attackTime(160.0f),
		_attacking(false)
	{
	}

	void Raven::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Raven"_s);
	}

	Task<bool> Raven::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		SetHealthByDifficulty(2);
		_scoreValue = 300;

		_originPos = _lastPos = _targetPos = _pos;

		async_await RequestMetadataAsync("Enemy/Raven"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Raven::OnUpdate(float timeMult)
	{
		OnUpdateHitbox();
		HandleBlinking(timeMult);
		UpdateFrozenState(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (_currentTransition == nullptr) {
			if (_attackTime > 0.0f) {
				_attackTime -= timeMult;
			} else {
				if (_attacking) {
					SetAnimation(AnimState::Idle);

					_targetPos = _originPos;

					_attackTime = 120.0f;
					_attacking = false;
				} else {
					AttackNearestPlayer();
				}
			}
		}

		_anglePhase += timeMult * 0.04f;

		if ((_targetPos - _lastPos).Length() > 5.0f) {
			Vector2f dir = (_targetPos - _lastPos).Normalized();
			Vector2f speed = {
				dir.X * lerp(_lastSpeed.X, _attacking ? 2.8f : 1.4f, 1.6f * timeMult),
				dir.Y * lerp(_lastSpeed.Y, _attacking ? 2.8f : 1.4f, 1.6f * timeMult)
			};
			_lastPos.X += speed.X * timeMult;
			_lastPos.Y += speed.Y * timeMult;
			_lastSpeed = speed;

			bool willFaceLeft = (speed.X < 0.0f);
			if (IsFacingLeft() != willFaceLeft) {
				SetTransition(AnimState::TransitionTurn, false, [this, willFaceLeft]() {
					SetFacingLeft(willFaceLeft);
				});
			}
		}

		MoveInstantly(_lastPos + Vector2f(0.0f, sinf(_anglePhase) * 6.0f), MoveType::Absolute | MoveType::Force);
	}

	bool Raven::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	void Raven::AttackNearestPlayer()
	{
		bool found = false;
		Vector2f foundPos = Vector2f::Zero;
		float targetDistance = 300.0f;
		auto& players = _levelHandler->GetPlayers();
		for (auto& player : players) {
			Vector2f newPos = player->GetPos();
			float distance = (_lastPos - newPos).Length();
			if (distance < targetDistance) {
				foundPos = newPos;
				targetDistance = distance;
				found = true;
			}
		}

		if (found) {
			SetAnimation(AnimState::TransitionAttack);

			// Can't fly into the water
			float waterLevel = _levelHandler->WaterLevel() - 12.0f;
			if (foundPos.Y > waterLevel - 8.0f) {
				foundPos.Y = waterLevel - 8.0f;
			}

			_targetPos = foundPos;
			_targetPos.Y -= 30.0f;

			_attackTime = 80.0f;
			_attacking = true;

			PlaySfx("Attack"_s, 0.7f, Random().NextFloat(1.4f, 1.8f));
		}
	}
}