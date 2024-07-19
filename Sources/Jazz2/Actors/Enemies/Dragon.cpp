#include "Dragon.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Dragon::Dragon()
		:
		_attacking(false),
		_stateTime(0.0f),
		_attackTime(0.0f)
	{
	}

	void Dragon::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Dragon"_s);
		PreloadMetadataAsync("Weapon/Toaster"_s);
	}

	Task<bool> Dragon::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(1);
		_scoreValue = 200;

		async_await RequestMetadataAsync("Enemy/Dragon"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Dragon::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		bool found = false;
		Vector2f targetPos;
		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			Vector2f newPos = player->GetPos();
			if ((newPos - _pos).Length() <= 220.0f) {
				targetPos = newPos;
				found = true;
				break;
			}
		}

		if (found) {
			if (_currentTransition == nullptr) {
				if (!_attacking) {
					if (_stateTime <= 0.0f) {
						bool willFaceLeft = (_pos.X > targetPos.X);
						if (IsFacingLeft() != willFaceLeft) {
							SetTransition(AnimState::TransitionTurn, false, [this, willFaceLeft]() {
								SetFacingLeft(willFaceLeft);

								SetAnimation((AnimState)1073741825);
								SetTransition((AnimState)1073741824, false, [this]() {
									_attacking = true;
									_stateTime = 30.f;
								});
							});
						} else {
							SetAnimation((AnimState)1073741825);
							SetTransition((AnimState)1073741824, false, [this]() {
								_attacking = true;
								_stateTime = 30.0f;
							});
						}
					}
				} else {
					if (_stateTime <= 0.0f) {
						SetAnimation(AnimState::Idle);
						SetTransition((AnimState)1073741826, false, [this]() {
							_attacking = false;
							_stateTime = 60.0f;
						});
					} else {
						if (_attackTime <= 0.0f) {
							std::shared_ptr<Fire> fire = std::make_shared<Fire>();
							uint8_t fireParams[1];
							fireParams[0] = (IsFacingLeft() ? 1 : 0);
							fire->OnActivated(ActorActivationDetails(
								_levelHandler,
								Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -14 : 14), (std::int32_t)_pos.Y - 6, _renderer.layer() + 2),
								fireParams
							));
							_levelHandler->AddActor(fire);

							_attackTime = 10.0f;
						} else {
							_attackTime -= timeMult;
						}
					}
				}
			}

			_stateTime -= timeMult;
		} else if (_attacking) {
			SetAnimation(AnimState::Idle);
			SetTransition((AnimState)1073741826, false, [this]() {
				_attacking = false;
				_stateTime = 80.0f;
			});
		}
	}

	bool Dragon::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() - 2), Explosion::Type::Tiny);

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	Task<bool> Dragon::Fire::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);
		// Collide with player ammo if Reforged
		CanCollideWithAmmo = _levelHandler->IsReforged();

		SetFacingLeft(details.Params[0] != 0);

		async_await RequestMetadataAsync("Weapon/Toaster"_s);
		SetAnimation(AnimState::Default);

		constexpr float BaseSpeed = 1.6f;
		_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * (BaseSpeed + Random().NextFloat(0.0f, 0.2f));
		_speed.Y += Random().NextFloat(-0.5f, 0.5f);

		_timeLeft = 60.0f;

		async_return true;
	}

	void Dragon::Fire::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_timeLeft <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		} else {
			_timeLeft -= timeMult;
		}
	}

	void Dragon::Fire::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light1 = lights.emplace_back();
		light1.Pos = _pos;
		light1.Intensity = 0.85f;
		light1.Brightness = 0.6f;
		light1.RadiusNear = 0.0f;
		light1.RadiusFar = 30.0f;

		auto& light2 = lights.emplace_back();
		light2.Pos = _pos;
		light2.Intensity = 0.2f;
		light2.RadiusNear = 0.0f;
		light2.RadiusFar = 140.0f;
	}
}