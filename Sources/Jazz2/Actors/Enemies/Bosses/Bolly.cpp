#include "Bolly.h"
#include "../../../ILevelHandler.h"
#include "../Crab.h"
#include "../../Player.h"
#include "../../Explosion.h"
#include "../../Weapons/ShotBase.h"

#include "../../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Bosses
{
	Bolly::Bolly()
		:
		_state(StateWaiting),
		_stateTime(0.0f),
		_endText(0),
		_noiseCooldown(0.0f),
		_rocketsLeft(0),
		_chainPhase(0.0f)
	{
	}

	Bolly::~Bolly()
	{
		if (_bottom != nullptr) {
			_bottom->SetState(ActorState::IsDestroyed, true);
		}
		/*if (_turret != nullptr) {
			_turret->SetState(ActorState::IsDestroyed, true);
		}*/
		for (int32_t i = 0; i < static_cast<int32_t>(arraySize(_chain)); i++) {
			if (_chain[i] != nullptr) {
				_chain[i]->SetState(ActorState::IsDestroyed, true);
			}
		}
	}

	void Bolly::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Boss/Bolly"_s);
	}

	Task<bool> Bolly::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_endText = details.Params[1];
		_originPos = _pos;

		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);

		_scoreValue = 3000;

		async_await RequestMetadataAsync("Boss/Bolly"_s);
		SetAnimation(AnimState::Idle);

		_bottom = std::make_shared<BollyPart>();
		uint8_t bottomParams[1] = { 1 };
		_bottom->OnActivated(ActorActivationDetails(
			_levelHandler,
			Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() + 2),
			bottomParams
		));
		_levelHandler->AddActor(_bottom);

		/*_turret = std::make_shared<BollyPart>();
		uint8_t turretParams[1] = { 2 };
		_turret->OnActivated({
			.LevelHandler = _levelHandler,
			.Pos = Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() + 6),
			.Params = turretParams
		});
		_levelHandler->AddActor(_turret);*/

		int32_t chainLength = (_levelHandler->Difficulty() < GameDifficulty::Hard ? NormalChainLength : HardChainLength);
		for (int32_t i = 0; i < chainLength; i++) {
			_chain[i] = std::make_shared<BollyPart>();
			uint8_t chainParams[1] = { (uint8_t)((i % 3) == 2 ? 3 : 4) };
			_chain[i]->OnActivated(ActorActivationDetails(
				_levelHandler,
				Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() - ((i % 3) == 2 ? 2 : 4)),
				chainParams
			));
			_levelHandler->AddActor(_chain[i]);
		}

		async_return true;
	}

	bool Bolly::OnActivatedBoss()
	{
		SetHealthByDifficulty(42);

		MoveInstantly(_originPos, MoveType::Absolute | MoveType::Force);

		FollowNearestPlayer(StateFlying, 100.0f);
		return true;
	}

	void Bolly::OnUpdate(float timeMult)
	{
		HandleBlinking(timeMult);

		MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);

		switch (_state) {
			case StateFlying: {
				if (_stateTime <= 0.0f) {
					if (Random().NextFloat() < 0.1f) {
						_state = StatePrepairingToAttack;
						_stateTime = 120.0f;
						_speed.X = 0;
						_speed.Y = 0;
					} else {
						_state = StateNewDirection;
						_stateTime = 50.0f;
					}
				}
				break;
			}

			case StateNewDirection: {
				if (_stateTime <= 0.0f) {
					FollowNearestPlayer(StateFlying, 1.0f);
				}
				break;
			}

			case StatePrepairingToAttack: {
				if (_stateTime <= 0.0f) {
					_state = StateAttacking;
					_stateTime = 20.0f;
					_rocketsLeft = 5;

					PlaySfx("PreAttack"_s);
				}
				break;
			}

			case StateAttacking: {
				if (_stateTime <= 0.0f) {
					if (_rocketsLeft > 0) {
						_stateTime = 20.0f;

						FireRocket();
						_rocketsLeft--;

						PlaySfx("Attack"_s);
					} else {
						_state = StateNewDirection;
						_stateTime = 100.0f;

						PlaySfx("PostAttack"_s);
					}
				}
				break;
			}
		}

		if (_bottom != nullptr) {
			_bottom->MoveInstantly(Vector2f(_pos.X + _speed.X * timeMult, _pos.Y + _speed.Y * timeMult), MoveType::Absolute | MoveType::Force);
			_bottom->SetFacingLeft(IsFacingLeft());
		}
		/*if (_turret != nullptr) {
			UpdateTurret(timeMult);
		}*/

		float distance = 30.0f;
		for (int i = 0; i < static_cast<int>(arraySize(_chain)); i++) {
			if (_chain[i] != nullptr) {
				float angle = sinf(_chainPhase - i * 0.08f) * 1.2f + fPiOver2;

				Vector2f piecePos = _pos;
				piecePos.X += cosf(angle) * distance;
				piecePos.Y += sinf(angle) * distance;
				_chain[i]->MoveInstantly(piecePos, MoveType::Absolute | MoveType::Force);

				distance += _chain[i]->Size;
			}
		}

		if (_noiseCooldown > 0.0f) {
			_noiseCooldown -= timeMult;
		} else {
			_noiseCooldown = 120.0f;
			PlaySfx("Noise"_s, 0.2f);
		}

		_stateTime -= timeMult;
		_chainPhase += timeMult * 0.06f;
	}

	bool Bolly::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() + 2), Explosion::Type::Large);

		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		StringView text = _levelHandler->GetLevelText(_endText);
		_levelHandler->ShowLevelText(text);

		return BossBase::OnPerish(collider);
	}

	/*void Bolly::UpdateTurret(float timeMult)
	{
		bool found = false;
		Vector2f targetPos = Vector2f(FLT_MAX, FLT_MAX);

		auto& players = _levelHandler->GetPlayers();
		for (auto& player : players) {
			Vector2f newPos = player->GetPos();
			if ((_pos - newPos).Length() < (_pos - targetPos).Length()) {
				targetPos = newPos;
				found = true;
			}
		}

		if (found) {
			bool facingLeft = (targetPos.X < _pos.X);
			Vector2f diff = (targetPos - _pos).Normalized();
			_turret->_renderer.setRotation(facingLeft ? atan2f(-diff.Y, -diff.X) : atan2f(diff.Y, diff.X));
			_turret->SetFacingLeft(facingLeft);
		}

		_turret->MoveInstantly(Vector2f(_pos.X + (IsFacingLeft() ? 10.0f : -10.0f) + _speed.X * timeMult, _pos.Y + 10.0f + _speed.Y * timeMult), MoveType::Absolute | MoveType::Force);
	}*/

	void Bolly::FollowNearestPlayer(int newState, float time)
	{
		bool found = false;
		Vector2f targetPos = Vector2f(FLT_MAX, FLT_MAX);

		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			Vector2f newPos = player->GetPos();
			if ((_pos - newPos).SqrLength() < (_pos - targetPos).SqrLength()) {
				targetPos = newPos;
				found = true;
			}
		}

		if (found) {
			_state = newState;
			_stateTime = time;

			targetPos.Y += Random().FastFloat(-100.0f, 20.0f);

			SetFacingLeft(targetPos.X < _pos.X);

			float speedMultiplier = (_levelHandler->Difficulty() < GameDifficulty::Hard ? 0.6f : 0.8f);
			Vector2f speed = (targetPos - _pos).Normalized();
			_speed.X = speed.X * speedMultiplier;
			_speed.Y = speed.Y * speedMultiplier;
		}
	}

	void Bolly::FireRocket()
	{
		bool found = false;
		Vector2f targetPos = Vector2f(FLT_MAX, FLT_MAX);

		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			Vector2f newPos = player->GetPos();
			if ((_pos - newPos).SqrLength() < (_pos - targetPos).SqrLength()) {
				targetPos = newPos;
				found = true;
			}
		}

		if (found) {
			Vector2f diff = (targetPos - _pos).Normalized();

			std::shared_ptr<Rocket> rocket = std::make_shared<Rocket>();
			rocket->OnActivated(ActorActivationDetails(
				_levelHandler,
				Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? 10 : -10), (std::int32_t)_pos.Y + 10, _renderer.layer() - 4)
			));
			rocket->_renderer.setRotation(atan2f(diff.Y, diff.X));
			_levelHandler->AddActor(rocket);
		}
	}

	Task<bool> Bolly::BollyPart::OnActivatedAsync(const ActorActivationDetails& details)
	{
		uint8_t partType = details.Params[0];

		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);

		_health = INT32_MAX;

		async_await RequestMetadataAsync("Boss/Bolly"_s);
		SetAnimation((AnimState)partType);

		switch (partType) {
			case 1: // Bottom
				SetState(ActorState::IsSolidObject, true);
				SetState(ActorState::CollideWithOtherActors, false);
				CanCollideWithAmmo = true;
				Size = 0.0f;
				break;
			case 2: // Turret
				SetState(ActorState::CollideWithOtherActors, false);
				CanCollideWithAmmo = false;
				Size = 0.0f;
				break;
			case 3: // Chain 1
				CanCollideWithAmmo = false;
				Size = 14.0f;
				break;
			case 4: // Chain 2
				CanCollideWithAmmo = false;
				Size = 7.0f;
				break;
		}

		async_return true;
	}

	void Bolly::BollyPart::OnUpdate(float timeMult)
	{
	}

	Task<bool> Bolly::Rocket::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);
		CanCollideWithAmmo = false;

		_timeLeft = 300.0f;
		_health = INT32_MAX;

		async_await RequestMetadataAsync("Boss/Bolly"_s);
		SetAnimation((AnimState)5);

		async_return true;
	}

	void Bolly::Rocket::OnUpdate(float timeMult)
	{
		float angle = _renderer.rotation();
		_speed.X += cosf(angle) * 0.14f * timeMult;
		_speed.Y += sinf(angle) * 0.14f * timeMult;

		EnemyBase::OnUpdate(timeMult);

		if (_timeLeft <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		} else {
			_timeLeft -= timeMult;
		}
	}

	void Bolly::Rocket::OnUpdateHitbox()
	{
		UpdateHitbox(20, 20);
	}

	void Bolly::Rocket::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = 0.8f;
		light.Brightness = 0.8f;
		light.RadiusNear = 3.0f;
		light.RadiusFar = 12.0f;
	}

	bool Bolly::Rocket::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* player = runtime_cast<Player*>(other)) {
			DecreaseHealth(INT32_MAX);
		}

		return ActorBase::OnHandleCollision(other);
	}

	bool Bolly::Rocket::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() + 2), Explosion::Type::RF);

		return EnemyBase::OnPerish(collider);
	}

	void Bolly::Rocket::OnHitFloor(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void Bolly::Rocket::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void Bolly::Rocket::OnHitCeiling(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}
}