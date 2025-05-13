#include "Rapier.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Enemies
{
	Rapier::Rapier()
		: _anglePhase(0.0f), _attackTime(80.0f), _attacking(false), _noiseCooldown(Random().FastFloat(200.0f, 400.0f))
	{
	}

	void Rapier::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Rapier"_s);
	}

	Task<bool> Rapier::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::ApplyGravitation, false);

		SetHealthByDifficulty(2);
		_scoreValue = 300;

		_originPos = _lastPos = _targetPos = _pos;

		async_await RequestMetadataAsync("Enemy/Rapier"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Rapier::OnUpdate(float timeMult)
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
					SetTransition((AnimState)1073741826, false, [this]() {
						_targetPos = _originPos;

						_attackTime = Random().NextFloat(200.0f, 260.0f);
						_attacking = false;
					});
				} else {
					AttackNearestPlayer();
				}
			}

			if (_noiseCooldown > 0.0f) {
				_noiseCooldown -= timeMult;
			} else {
				_noiseCooldown = Random().FastFloat(300.0f, 600.0f);

				if (Random().NextFloat() < 0.5f) {
					PlaySfx("Noise"_s, 0.7f);
				}
			}
		}

		_anglePhase += timeMult * 0.02f;

		float speedX = ((_targetPos.X - _lastPos.X) * timeMult / 26.0f + _lastSpeed.X * 1.4f) / 2.4f;
		float speedY = ((_targetPos.Y - _lastPos.Y) * timeMult / 26.0f + _lastSpeed.Y * 1.4f) / 2.4f;
		_lastPos.X += speedX;
		_lastPos.Y += speedY;
		_lastSpeed = Vector2f(speedX, speedY);

		bool willFaceLeft = (_speed.X < 0.0f);
		if (IsFacingLeft() != willFaceLeft) {
			SetTransition(AnimState::TransitionTurn, false, [this, willFaceLeft]() {
				SetFacingLeft(willFaceLeft);
			});
		}

		MoveInstantly(_lastPos + Vector2f(cosf(_anglePhase) * 10.0f, sinf(_anglePhase * 2.0f) * 10.0f), MoveType::Absolute | MoveType::Force);
	}

	bool Rapier::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(ParticleDebrisEffect::Dissolve, Vector2f::Zero);
		PlaySfx("Die"_s);
		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	void Rapier::AttackNearestPlayer()
	{
		bool found = false;
		Vector2f foundPos = Vector2f(FLT_MAX, FLT_MAX);

		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			Vector2f newPos = player->GetPos();
			if ((_lastPos - newPos).SqrLength() < (_lastPos - foundPos).SqrLength()) {
				foundPos = newPos;
				found = true;
			}
		}

		Vector2f diff = (foundPos - _lastPos);
		if (found && diff.Length() <= 200.0f) {
			SetAnimation((AnimState)1073741824);
			SetTransition((AnimState)1073741825, false, [this, foundPos]() {
				_targetPos = foundPos;

				_attackTime = 80.0f;
				_attacking = true;

				PlaySfx("Attack"_s, 0.7f);
			});
		}
	}
}