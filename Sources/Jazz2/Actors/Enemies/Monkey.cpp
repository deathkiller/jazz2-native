#include "Monkey.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

#include <algorithm>
#include <float.h>

namespace Jazz2::Actors::Enemies
{
	Monkey::Monkey()
		: _time(0.0f), _isWalking(false), _stuck(false), _standing(false), _hasStood(false)
	{
	}

	float Monkey::GetWalkSpeed() const
	{
		// The original speed follows the walk animation, slowest at its middle frame
		std::int32_t frame = std::max<std::int32_t>(_renderer.CurrentFrame - _renderer.FirstFrame, 0);
		std::int32_t step = (std::abs(frame - 7) * 32) & 511;
		return (1.0f + sinApprox((float)step * (fTwoPi / 1024.0f))) * 0.5f * TicksPerFrame;
	}

	float Monkey::GetThrowInterval() const
	{
		std::int32_t difficultyLevel = 1;
		switch (_levelHandler->GetDifficulty()) {
			case GameDifficulty::Easy: difficultyLevel = 0; break;
			case GameDifficulty::Hard: difficultyLevel = 2; break;
			default: break;
		}
		return (3 - difficultyLevel) * ThrowWaitTicks / TicksPerFrame;
	}

	bool Monkey::HasPlayerInSight() const
	{
		return (FindNearestPlayer(SightDistance * SightDistance) != nullptr);
	}

	void Monkey::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Monkey"_s);
	}

	Task<bool> Monkey::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_isWalking = (details.Params[0] != 0);

		SetHealthByDifficulty(3);
		_scoreValue = (_isWalking ? 200 : 300);

		async_await RequestMetadataAsync("Enemy/Monkey"_s);

		if (_isWalking) {
			SetFacingLeft(Random().NextBool());
			SetAnimation(AnimState::Walk);

			_speed.X = (IsFacingLeft() ? -1 : 1) * GetWalkSpeed();

			PlaceOnGround();
		} else {
			SetAnimation(AnimState::Jump);
		}

		async_return true;
	}

	void Monkey::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (!_isWalking) {
			// The standing one throws once the wait for its difficulty has passed
			if (_currentTransition == nullptr) {
				_time += timeMult;
				if (_time >= GetThrowInterval() && TryThrow()) {
					_time = 0.0f;
				}
			}
			return;
		}

		if (_standing) {
			_time -= timeMult;
			if (_time <= 0.0f) {
				_standing = false;
				_time = 0.0f;
				SetAnimation(AnimState::Walk);
				if (!TryThrow()) {
					_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * GetWalkSpeed();
				}
			}
			return;
		}

		if (_currentTransition == nullptr) {
			if (std::abs(_speed.X) > 0.0f) {
				_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * GetWalkSpeed();
			}

			// It stops for a moment after walking for a while without a player in sight
			if (HasPlayerInSight()) {
				_time = 0.0f;
				// The original stops to throw as soon as it sees a player
				if (TryThrow()) {
					return;
				}
			} else {
				_time += timeMult;
				if (_time > WalkTicks / TicksPerFrame) {
					_time = 0.0f;
					_speed.X = 0.0f;
					if (_hasStood) {
						// Measured in JJ2+: only its first stop is a long one, later it just pauses
						SetTransition((AnimState)1073741824, false, [this]() {
							if (!TryThrow()) {
								_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * GetWalkSpeed();
							}
						});
					} else {
						_hasStood = true;
						SetTransition((AnimState)1073741825, false, [this]() {
							_standing = true;
							_time = StandTicks / TicksPerFrame;
							SetAnimation(AnimState::Jump);
						});
					}
					return;
				}
			}
		}

		if (GetState(ActorState::CanJump) && std::abs(_speed.X) > 0) {
			if (!CanMoveToPosition(_speed.X * 4, 0)) {
				if (_stuck) {
					MoveInstantly(Vector2f(0.0f, -2.0f), MoveType::Relative | MoveType::Force);
				} else {
					SetFacingLeft(!IsFacingLeft());
					_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * GetWalkSpeed();
					_stuck = true;
				}
			} else {
				_stuck = false;
			}
		}
	}

	void Monkey::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		std::uint8_t flags = 0;
		if (_stuck) flags |= 0x01;
		if (_currentTransition != nullptr) flags |= 0x02;
		dest.WriteValue<std::uint8_t>(flags);
	}

	void Monkey::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		std::uint8_t flags = src.ReadValue<std::uint8_t>();
		_stuck = ((flags & 0x01) != 0);
		if (_isWalking && (flags & 0x02) != 0) {
			// Throwing was interrupted, continue walking as the last transition callback would
			_speed.X = (IsFacingLeft() ? -1 : 1) * GetWalkSpeed();
		}
	}

	void Monkey::OnUpdateHitbox()
	{
		UpdateHitbox(30, 30);
	}

	void Monkey::OnAnimationFinished()
	{
		bool transitionEnded = (_currentTransition != nullptr);
		EnemyBase::OnAnimationFinished();

		if (transitionEnded) {
			return;
		}

		// Only a walking monkey throws when its animation ends, a standing one waits for its difficulty's time
		if (_isWalking && !_standing) {
			TryThrow();
		}
	}

	bool Monkey::TryThrow()
	{
		if (_currentTransition == nullptr) {
			bool found = false;
			Vector2f targetPos;
			float targetDistanceSqr = FLT_MAX;

			for (auto* player : _levelHandler->GetPlayers()) {
				if (player->GetHealth() <= 0) {
					continue;
				}

				Vector2f newPos = player->GetPos();
				float distanceSqr = (_pos - newPos).SqrLength();
				if (distanceSqr < targetDistanceSqr) {
					targetPos = newPos;
					targetDistanceSqr = distanceSqr;
					found = true;
				}
			}

			if (found) {
				Vector2f diff = (targetPos - _pos);
				std::uint32_t distance = (std::uint32_t)std::abs(diff.X);
				if (distance > 16.0f && targetDistanceSqr < SightDistance * SightDistance) {
					_time = 0.0f;
					if (_isWalking) {
						_speed.X = 0.0f;
						_hasStood = true;

						SetTransition((AnimState)1073741825, false, [this, targetPos, distance]() {
							SetFacingLeft(targetPos.X < _pos.X);

							SetTransition((AnimState)1073741826, false, [this, distance]() {
								ThrowBanana(distance, 8);

								SetTransition((AnimState)1073741827, false, [this]() {

									SetTransition((AnimState)1073741824, false, [this]() {
										// Measured in JJ2+: it keeps throwing while a player is in range
										if (!TryThrow()) {
											SetFacingLeft(Random().NextBool());
											_speed.X = (IsFacingLeft() ? -1 : 1) * GetWalkSpeed();
										}
									});
								});
							});
						});
					} else {
						SetFacingLeft(targetPos.X < _pos.X);

						SetTransition((AnimState)1073741826, false, [this, distance]() {
							ThrowBanana(distance, 42);
							SetTransition((AnimState)1073741827, false);
						});
					}
					return true;
				}
			}
		}
		return false;
	}

	void Monkey::ThrowBanana(std::uint32_t distance, std::int32_t offsetX)
	{
		std::shared_ptr<Banana> banana = std::make_shared<Banana>();
		std::uint8_t bananaParams[3];
		bananaParams[0] = (IsFacingLeft() ? 1 : 0);
		bananaParams[1] = distance & 0xff;
		bananaParams[2] = (distance >> 8) & 0xff;
		banana->OnActivated(ActorActivationDetails(
			_levelHandler,
			Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -offsetX : offsetX), (std::int32_t)_pos.Y - 8, _renderer.GetLayer() + 2),
			bananaParams
		));
		_levelHandler->AddActor(banana);
	}

	bool Monkey::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	Task<bool> Monkey::Banana::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_health = 2;
		SetFacingLeft(details.Params[0] != 0);

		std::uint32_t distance = details.Params[1] | (details.Params[2] << 8);
		_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * distance / 32;
		_speed.Y = -3.0f;

		async_await RequestMetadataAsync("Enemy/Monkey"_s);
		SetAnimation((AnimState)1073741828);

		_soundThrow = PlaySfx("BananaThrow"_s);

		async_return true;
	}

	void Monkey::Banana::OnUpdateHitbox()
	{
		UpdateHitbox(4, 4);
	}

	bool Monkey::Banana::OnPerish(ActorBase* collider)
	{
		_soundThrow = nullptr;

		_speed.X = 0.0f;
		_speed.Y = 0.0f;

		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		SetTransition((AnimState)1073741829, false, [this, collider]() {
			EnemyBase::OnPerish(collider);
		});

		PlaySfx("BananaSplat"_s, 0.6f);

		return false;
	}

	void Monkey::Banana::OnHitFloor(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void Monkey::Banana::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void Monkey::Banana::OnHitCeiling(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}
}