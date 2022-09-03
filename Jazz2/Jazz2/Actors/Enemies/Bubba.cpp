#include "Bubba.h"
#include "../../ILevelHandler.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Enemies
{
	Bubba::Bubba()
		:
		_state(StateWaiting),
		_stateTime(0.0f),
		_endText(0)
	{
	}

	void Bubba::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Boss/Bubba"_s);
	}

	Task<bool> Bubba::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_endText = details.Params[1];

		SetHealthByDifficulty(93);
		_scoreValue = 4000;

		co_await RequestMetadataAsync("Boss/Bubba"_s);
		SetAnimation(AnimState::Idle);

		co_return true;
	}

	bool Bubba::OnActivatedBoss()
	{
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
				if (GetState(ActorFlags::CanJump)) {
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

								// TODO: Fireball
								/*Fireball fireball = new Fireball();
								fireball.OnActivated(new ActorActivationDetails {
									LevelHandler = levelHandler,
									Pos = new Vector3(_pos.X + x, _pos.Y + y, _pos.Z + 2f),
									Params = new[] { (ushort)(IsFacingLeft ? 1 : 0) }
								});
								levelHandler.AddActor(fireball);*/

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
						CollisionFlags = CollisionFlags::CollideWithTileset | CollisionFlags::CollideWithOtherActors | CollisionFlags::ApplyGravitation;

						_state = StateFalling;

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
		CreateParticleDebris();
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		StringView text = _levelHandler->GetLevelText(_endText, -1, '|');
		_levelHandler->ShowLevelText(text);

		_speed.X = 0.0f;
		_speed.Y = -2.0f;
		_internalForceY = 0.0f;
		_frozenTimeLeft = 0.0f;

		CollisionFlags = CollisionFlags::None;

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

			_internalForceY = 1.27f;

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
			if ((_pos - newPos).Length() < (_pos - targetPos).Length()) {
				targetPos = newPos;
				found = true;
			}
		}

		if (found) {
			_state = StateTornado;
			_stateTime = 60.0f;

			CollisionFlags = CollisionFlags::CollideWithTileset | CollisionFlags::CollideWithOtherActors;

			MoveInstantly(Vector2f(0, -1), MoveType::Relative);

			Vector2f diff = (targetPos - _pos);

			SetTransition((AnimState)1073741830, false, [this, diff]() {
				_speed.X = (diff.X / _stateTime);
				_speed.Y = (diff.Y / _stateTime);
				_internalForceY = 0;
				_externalForce.Y = 0;

				SetAnimation((AnimState)1073741831);
			});
		}
	}
}