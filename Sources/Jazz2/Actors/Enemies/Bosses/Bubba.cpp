#include "Bubba.h"
#include "../../../ILevelHandler.h"
#include "../../Player.h"
#include "../../Explosion.h"

#include "../../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Bosses
{
	Bubba::Bubba()
		: _state(StateWaiting), _stateTime(0.0f), _endText(0)
	{
	}

	void Bubba::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Boss/Bubba"_s);
	}

	Task<bool> Bubba::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_endText = details.Params[1];
		_scoreValue = 4000;

		SetState(ActorState::CollideWithTilesetReduced, true);

		async_await RequestMetadataAsync("Boss/Bubba"_s);
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	bool Bubba::OnActivatedBoss()
	{
		SetHealthByDifficulty(93);
		FollowNearestPlayer();
		return true;
	}

	void Bubba::OnUpdate(float timeMult)
	{
		BossBase::OnUpdate(timeMult);

		// Process level bounds
		Recti levelBounds = _levelHandler->LevelBounds();
		if (_pos.X < levelBounds.X) {
			_pos.X = levelBounds.X;
		} else if (_pos.X > levelBounds.X + levelBounds.W) {
			_pos.X = levelBounds.X + levelBounds.W;
		}

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		switch (_state) {
			case StateJumping: {
				if (_speed.Y > 0.0f) {
					_state = StateFalling;
					SetAnimation(AnimState::Fall);
				}
				break;
			}

			case StateFalling: {
				if (GetState(ActorState::CanJump)) {
					_speed.Y = 0.0f;
					_speed.X = 0.0f;

					_state = StateTransition;
					SetTransition(AnimState::TransitionFallToIdle, false, [this]() {
						float rand = Random().NextFloat();
						bool spewFileball = (rand < 0.35f);
						bool tornado = (rand < 0.65f);
						if (spewFileball) {
							PlaySfx("Sneeze"_s);

							SetTransition(AnimState::Shoot, false, [this]() {
								float x = (IsFacingLeft() ? -16.0f : 16.0f);
								float y = -5.0f;

								std::shared_ptr<Fireball> fireball = std::make_shared<Fireball>();
								uint8_t fireballParams[1] = { (uint8_t)(IsFacingLeft() ? 1 : 0) };
								fireball->OnActivated(ActorActivationDetails(
									_levelHandler,
									Vector3i((std::int32_t)(_pos.X + x), (std::int32_t)(_pos.Y + y), _renderer.layer() - 2),
									fireballParams
								));
								_levelHandler->AddActor(fireball);

								SetTransition(AnimState::TransitionShootToIdle, false, [this]() {
									FollowNearestPlayer();
								});
							});
						} else if (tornado) {
							TornadoToNearestPlayer();
						} else {
							FollowNearestPlayer();
						}
					});
				}
				break;
			}

			case StateTornado: {
				if (_stateTime <= 0.0f) {
					_state = StateTransition;
					SetTransition((AnimState)1073741832, false, [this]() {
						SetState(ActorState::CollideWithTilesetReduced | ActorState::ApplyGravitation, true);

						_state = StateFalling;

						if (_tornadoNoise != nullptr) {
							_tornadoNoise->stop();
							_tornadoNoise = nullptr;
						}
						SetAnimation((AnimState)1073741833);
					});
				}

				break;
			}

			case StateDying: {
				float time = (_renderer.AnimTime / _renderer.AnimDuration);
				_renderer.setColor(Colorf(1.0f, 1.0f, 1.0f, 1.0f - (time * time * time * time)));
				break;
			}
		}

		_stateTime -= timeMult;
	}

	void Bubba::OnUpdateHitbox()
	{
		UpdateHitbox(20, 24);
	}

	bool Bubba::OnPerish(ActorBase* collider)
	{
		// It must be done here, because the player may not exist after animation callback 
		AddScoreToCollider(collider);

		ForceCancelTransition();

		CreateParticleDebris();
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		StringView text = _levelHandler->GetLevelText(_endText);
		_levelHandler->ShowLevelText(text);

		_speed.X = 0.0f;
		_speed.Y = -2.0f;
		_externalForce.X = 0.0f;
		_externalForce.Y = 0.0f;
		_internalForceY = 0.0f;
		_frozenTimeLeft = std::min(1.0f, _frozenTimeLeft);

		SetState(ActorState::CollideWithTileset | ActorState::CollideWithTilesetReduced | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		_state = StateDying;
		SetTransition(AnimState::TransitionDeath, false, [this, collider]() {
			BossBase::OnPerish(collider);
		});

		return false;
	}

	void Bubba::FollowNearestPlayer()
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
			_state = StateJumping;
			_stateTime = 26;

			SetFacingLeft(targetPos.X < _pos.X);

			_speed.X = (IsFacingLeft() ? -1.3f : 1.3f);

			_internalForceY = -1.27f;

			PlaySfx("Jump"_s);

			SetTransition((AnimState)1073741825, false);
			SetAnimation(AnimState::Jump);
		}
	}

	void Bubba::TornadoToNearestPlayer()
	{
		bool found = false;
		Vector2f targetPos = Vector2f(FLT_MAX, FLT_MAX);

		auto& players = _levelHandler->GetPlayers();
		for (auto& player : players) {
			Vector2f newPos = player->GetPos();
			if ((_pos - newPos).SqrLength() < (_pos - targetPos).SqrLength()) {
				targetPos = newPos;
				found = true;
			}
		}

		if (found) {
			_state = StateTornado;
			_stateTime = 60.0f;

			SetState(ActorState::CollideWithTilesetReduced | ActorState::ApplyGravitation, false);

			Vector2f diff = (targetPos - _pos);

			_tornadoNoise = PlaySfx("Tornado"_s);
			SetTransition((AnimState)1073741830, false, [this, diff]() {
				_speed.X = (diff.X / _stateTime);
				_speed.Y = (diff.Y / _stateTime);
				_internalForceY = 0.0f;
				_externalForce.Y = 0.0f;

				SetAnimation((AnimState)1073741831);
			});
		}
	}

	Task<bool> Bubba::Fireball::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetFacingLeft(details.Params[0] != 0);
		_speed.X = (IsFacingLeft() ? -4.8f : 4.8f);
		_timeLeft = 50.0f;

		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);
		CanCollideWithAmmo = false;

		_health = INT32_MAX;

		async_await RequestMetadataAsync("Boss/Bubba"_s);
		SetAnimation((AnimState)1073741834);

		async_return true;
	}

	void Bubba::Fireball::OnUpdate(float timeMult)
	{
		MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);

		if (_timeLeft <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		} else {
			_timeLeft -= timeMult;
		}
	}

	void Bubba::Fireball::OnUpdateHitbox()
	{
		UpdateHitbox(18, 18);
	}

	void Bubba::Fireball::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = 0.85f;
		light.Brightness = 0.4f;
		light.RadiusNear = 0.0f;
		light.RadiusFar = 30.0f;
	}

	bool Bubba::Fireball::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* player = runtime_cast<Player*>(other)) {
			DecreaseHealth(INT32_MAX);
		}

		return ActorBase::OnHandleCollision(other);
	}

	bool Bubba::Fireball::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + _speed.X), (int)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::RF);

		return EnemyBase::OnPerish(collider);
	}
}