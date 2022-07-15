#include "Bat.h"
#include "../../LevelInitialization.h"
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
		PreloadMetadataAsync("Enemy/Bat");
	}

	Task<bool> Bat::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_originPos = _pos;

		CollisionFlags = CollisionFlags::CollideWithOtherActors;

		SetHealthByDifficulty(1);
		_scoreValue = 100;

		co_await RequestMetadataAsync("Enemy/Bat");
		SetAnimation(AnimState::Idle);

		co_return true;
	}

	void Bat::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0) {
			return;
		}

		SetState(ActorFlags::CanJump, false);

		Vector2f targetPos;
		if (FindNearestPlayer(targetPos)) {
			if (_attacking) {
				// Can't fly into the water
				if (targetPos.Y > _levelHandler->WaterLevel() - 20.0f) {
					targetPos.Y = _levelHandler->WaterLevel() - 20.0f;
				}

				Vector2f direction = (_pos - targetPos);
				direction.Normalize();
				_speed.X = direction.X * DefaultSpeed;
				_speed.Y = direction.Y * DefaultSpeed;

				SetFacingLeft(_speed.X < 0.0f);

				if (_noiseCooldown > 0.0f) {
					_noiseCooldown -= timeMult;
				} else {
					_noiseCooldown = 60.0f;
					//PlaySound("Noise");
				}
			} else {
				if (_currentTransitionState != AnimState::Idle) {
					return;
				}

				_speed.X = 0;
				_speed.Y = 0;

				SetAnimation(AnimState::Walk);

				SetTransition((AnimState)1073741824, false, [this]() {
					_attacking = true;
					SetTransition((AnimState)1073741825, false);
				});
			}
		} else {
			if (_attacking && _currentTransitionState == AnimState::Idle) {
				Vector2f direction = (_pos - _originPos);
				float length = direction.Length();
				if (length < 2.0f) {
					_attacking = false;
					MoveInstantly(_originPos, MoveType::Absolute, true);
					_speed.X = 0;
					_speed.Y = 0;
					SetAnimation(AnimState::Idle);
					SetTransition((AnimState)1073741826, false);
				} else {
					direction.Normalize();
					_speed.X = direction.X * DefaultSpeed;
					_speed.Y = direction.Y * DefaultSpeed;

					SetFacingLeft(_speed.X < 0.0f);
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
		// TODO
		//_levelHandler->PlayCommonSound("Splat", Transform.Pos);

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	bool Bat::FindNearestPlayer(Vector2f& targetPos)
	{
		constexpr float VisionDistanceIdle = 120.0f;
		constexpr float VisionDistanceAttacking = 320.0f;

		for (auto& player : _levelHandler->GetPlayers()) {
			targetPos = player->GetPos();
			float visionDistance = (_currentAnimationState == AnimState::Idle ? VisionDistanceIdle : VisionDistanceAttacking);
			if ((_originPos - targetPos).Length() < visionDistance) {
				return true;
			}
		}

		targetPos = Vector2f::Zero;
		return false;
	}
}