#include "Fencer.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Fencer::Fencer()
		: _stateTime(0.0f)
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

		async_await RequestMetadataAsync("Enemy/Fencer"_s);
		SetFacingLeft(true);
		SetAnimation(AnimState::Idle);

		PlaceOnGround();

		async_return true;
	}

	void Fencer::OnUpdate(float timeMult)
	{
		if (_frozenTimeLeft <= 0.0f) {
			Vector2f targetPos;
			if (FindNearestPlayer(targetPos)) {
				SetFacingLeft(_pos.X > targetPos.X);

				if (_stateTime <= 0.0f) {
					if (Random().NextFloat() < 0.3f) {
						_speed.X = (IsFacingLeft() ? -1 : 1) * 1.8f;
					} else {
						_speed.X = (IsFacingLeft() ? -1 : 1) * -1.2f;
					}

					MoveInstantly(Vector2f(0.0f, -4.0f), MoveType::Relative);
					_speed.Y = (_levelHandler->IsReforged() ? -3.0f : -2.0f);
					_internalForceY = -0.5f;

					PlaySfx("Attack"_s);

					SetTransition(AnimState::TransitionAttack, false, [this]() {
						_speed.X = 0.0f;
					});

					_stateTime = Random().NextFloat(160.0f, 300.0f);
				} else {
					_stateTime -= timeMult;
				}
			}
		}

		EnemyBase::OnUpdate(timeMult);
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