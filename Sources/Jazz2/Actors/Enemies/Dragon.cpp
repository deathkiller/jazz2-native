#include "Dragon.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Dragon::Dragon()
		: _stateTime(IdleTicks / TicksPerFrame), _attackTime(0.0f), _burstLeft(0), _attacking(false)
	{
	}

	void Dragon::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Dragon"_s);
		PreloadMetadataAsync("Weapon/Toaster"_s);
	}

	Task<bool> Dragon::OnActivatedAsync(const ActorActivationDetails& details)
	{
		// Move the dragon down a bit to avoid being stuck in the ceiling in the first level on hard difficulty
		MoveInstantly(Vector2f(0.0f, 8.0f), MoveType::Relative | MoveType::Force);

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
			if (player->GetHealth() <= 0) {
				continue;
			}

			Vector2f newPos = player->GetPos();
			if ((newPos - _pos).Length() < 256.0f) {
				targetPos = newPos;
				found = true;
				break;
			}
		}

		if (_currentTransition != nullptr) {
			return;
		}

		if (_attacking) {
			if (!found || _stateTime <= 0.0f) {
				// Measured in JJ2+: the whole attack is 32 ticks, it stands idle right after the flames
				SetAnimation(AnimState::Idle);
				_attacking = false;
				_stateTime = AttackPauseTicks / TicksPerFrame;
				return;
			}

			_stateTime -= timeMult;

			// The original spits a quick burst of flames
			_attackTime -= timeMult;
			while (_burstLeft > 0 && _attackTime <= 0.0f) {
				std::shared_ptr<Fire> fire = std::make_shared<Fire>();
				uint8_t fireParams[1];
				fireParams[0] = (IsFacingLeft() ? 1 : 0);
				fire->OnActivated(ActorActivationDetails(
					_levelHandler,
					Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -14 : 14), (std::int32_t)_pos.Y - 6, _renderer.GetLayer() + 2),
					fireParams
				));
				_levelHandler->AddActor(fire);

				_burstLeft--;
				_attackTime += BurstSpacingTicks / TicksPerFrame;
			}
			return;
		}

		// Idle time runs even without a player, it attacks only in the second half of it and turns around when it runs out
		_stateTime -= timeMult;
		if (found && _stateTime <AttackReadyTicks / TicksPerFrame) {
			bool willFaceLeft = (_pos.X > targetPos.X);
			if (IsFacingLeft() != willFaceLeft) {
				Turn(willFaceLeft);
			} else {
				SetAnimation((AnimState)1073741825);
				SetTransition((AnimState)1073741824, false, [this]() {
					_attacking = true;
					_stateTime = AttackTicks / TicksPerFrame;
					_attackTime = 0.0f;
					_burstLeft = BurstFlames;
				});
				SetAnimationTicks(AttackWindUpTicks);
			}
		} else if (_stateTime <= 0.0f) {
			Turn(!IsFacingLeft());
		}
	}

	void Dragon::Turn(bool willFaceLeft)
	{
		SetTransition(AnimState::TransitionTurn, false, [this, willFaceLeft]() {
			SetFacingLeft(willFaceLeft);
			_stateTime = IdleTicks / TicksPerFrame;
		});
	}

	void Dragon::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteValue<std::uint8_t>(_attacking ? 1 : 0);
		dest.WriteValueAsLE<float>(_attacking ? _stateTime : 0.0f);
		dest.WriteValueAsLE<float>(_attackTime);
	}

	void Dragon::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		_attacking = (src.ReadValue<std::uint8_t>() != 0);
		float stateTime = src.ReadValueAsLE<float>();
		if (_attacking) {
			_stateTime = stateTime;
		}
		_attackTime = src.ReadValueAsLE<float>();
	}

	bool Dragon::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() - 2), Explosion::Type::Tiny);

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	Task<bool> Dragon::Fire::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);
		CanCollideWithShots = false;

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