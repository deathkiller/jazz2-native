#include "Bat.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Bat::Bat()
		:
		_attacking(false),
		_noiseCooldown(0.0f)
	{
	}

	void Bat::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Bat"_s);
	}

	Task<bool> Bat::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_originPos = _pos;

		SetState(ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		SetHealthByDifficulty(1);
		_scoreValue = 100;

		async_await RequestMetadataAsync("Enemy/Bat"_s);
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Bat::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		SetState(ActorState::CanJump, false);

		Vector2f targetPos;
		if (FindNearestPlayer(targetPos)) {
			if (_attacking) {
				// Can't fly into the water
				if (targetPos.Y > _levelHandler->WaterLevel() - 20.0f) {
					targetPos.Y = _levelHandler->WaterLevel() - 20.0f;
				}

				Vector2f direction = (_pos - targetPos);
				if (direction.Length() > 20.0f) {
					direction.Normalize();
					_speed.X = direction.X * DefaultSpeed;
					_speed.Y = direction.Y * DefaultSpeed;

					SetFacingLeft(_speed.X < 0.0f);
				}

				if (_noiseCooldown > 0.0f) {
					_noiseCooldown -= timeMult;
				} else {
					_noiseCooldown = 60.0f;
					PlaySfx("Noise"_s);
				}
			} else {
				if (_currentTransition != nullptr) {
					return;
				}

				_speed.X = 0;
				_speed.Y = 0;
				_noiseCooldown = 0.0f;

				SetAnimation(AnimState::Walk);

				SetTransition((AnimState)1073741824, false, [this]() {
					_attacking = true;
					SetTransition((AnimState)1073741825, false);
				});
			}
		} else {
			if (_attacking && _currentTransition == nullptr) {
				Vector2f direction = (_pos - _originPos);
				float length = direction.Length();
				if (length < 2.0f) {
					_attacking = false;
					MoveInstantly(_originPos, MoveType::Absolute | MoveType::Force);
					_speed.X = 0.0f;
					_speed.Y = 0.0f;
					_noiseCooldown = 210.0f;
					SetAnimation(AnimState::Idle);
					SetTransition((AnimState)1073741826, false);
				} else {
					direction.Normalize();
					_speed.X = direction.X * DefaultSpeed;
					_speed.Y = direction.Y * DefaultSpeed;

					SetFacingLeft(_speed.X < 0.0f);
				}
			} else {
				if (_noiseCooldown > 0.0f) {
					_noiseCooldown -= timeMult;
				} else {
					_noiseCooldown = 210.0f;
					SetTransition((AnimState)1073741827, true);
				}
			}
		}
	}

	void Bat::OnUpdateHitbox()
	{
		UpdateHitbox(24, 24);
	}

	bool Bat::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	bool Bat::FindNearestPlayer(Vector2f& targetPos)
	{
		constexpr float VisionDistanceIdle = 120.0f;
		constexpr float VisionDistanceAttacking = 320.0f;

		for (auto& player : _levelHandler->GetPlayers()) {
			targetPos = player->GetPos();
			float visionDistance = (_currentAnimation->State == AnimState::Idle ? VisionDistanceIdle : VisionDistanceAttacking);
			if ((_originPos - targetPos).Length() < visionDistance) {
				return true;
			}
		}

		targetPos = Vector2f::Zero;
		return false;
	}
}