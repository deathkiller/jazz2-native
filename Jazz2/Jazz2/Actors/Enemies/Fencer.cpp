#include "Fencer.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Fencer::Fencer()
		:
		_stateTime(0.0f)
	{
	}

	void Fencer::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Fencer"_s);
	}

	Task<bool> Fencer::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(3);
		_scoreValue = 400;

		co_await RequestMetadataAsync("Enemy/Fencer"_s);
		SetFacingLeft(true);
		SetAnimation(AnimState::Idle);

		PlaceOnGround();

		co_return true;
	}

	void Fencer::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		Vector2f targetPos;
		if (FindNearestPlayer(targetPos)) {
			SetFacingLeft(_pos.X > targetPos.X);

			if (_stateTime <= 0.0f) {
				if (Random().NextFloat() < 0.3f) {
					_speed.X = (IsFacingLeft() ? -1 : 1) * 1.8f;
				} else {
					_speed.X = (IsFacingLeft() ? -1 : 1) * -1.2f;
				}
				_speed.Y = -4.5f;

				PlaySfx("Attack"_s);

				SetTransition(AnimState::TransitionAttack, false, [this]() {
					_speed.X = 0.0f;
					_speed.Y = 0.0f;
				});

				_stateTime = Random().NextFloat(180.0f, 300.0f);
			} else {
				_stateTime -= timeMult;
			}
		}
	}

	bool Fencer::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	bool Fencer::FindNearestPlayer(Vector2f& targetPos)
	{
		constexpr float VisionDistance = 100.0f;

		for (auto& player : _levelHandler->GetPlayers()) {
			targetPos = player->GetPos();
			if ((_pos - targetPos).Length() < VisionDistance) {
				return true;
			}
		}

		targetPos = Vector2f::Zero;
		return false;
	}
}