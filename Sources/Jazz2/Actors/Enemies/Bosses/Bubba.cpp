#include "Bubba.h"
#include "../../../ILevelHandler.h"
#include "../../Player.h"
#include "../../Explosion.h"

#include "../../../../nCine/Base/Random.h"

#include <cmath>

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Bosses
{
	Bubba::Bubba()
		: _state(State::Waiting), _ticks(0.0f), _tornadoSpeed(0.0f), _tornadoMoveTicks(0.0f), _fireballSpawned(false)
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
		_homePos = _pos;

		// All movement is driven by the state machine, it passes through walls except for the landing
		SetState(ActorState::CollideWithTilesetReduced | ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Boss/Bubba"_s);
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	bool Bubba::OnActivatedBoss()
	{
		// Only the original varies the health by difficulty itself, Reforged scales a fixed base instead
		if (_levelHandler->IsReforged()) {
			SetHealthByDifficulty(HealthByDifficulty[1]);
		} else {
			SetHealthByDifficulty(HealthByDifficulty[GetDifficultyIndex()]);
		}

		_homePos = _pos;
		SetFacingLeft(false);
		Decide();
		return true;
	}

	void Bubba::OnUpdate(float timeMult)
	{
		BossBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f || _state == State::Waiting) {
			return;
		}

		float ticks = timeMult * TicksPerFrame;

		switch (_state) {
			case State::Crouching: {
				_ticks += ticks;
				if (_ticks >= CrouchTicks) {
					_state = State::Rising;
					_vel.Y = JumpSpeedY;
					SetAnimation(AnimState::Jump);
					PlaySfx("Jump"_s);
				}
				break;
			}
			case State::Rising: {
				_vel.Y += Gravity * timeMult;
				MoveInstantly(Vector2f(_vel.X * timeMult, _vel.Y * timeMult), MoveType::Relative | MoveType::Force);
				if (_vel.Y >= 0.0f) {
					_state = State::Falling;
					SetAnimation(AnimState::Fall);
				}
				break;
			}
			case State::Falling: {
				_vel.Y = std::min<float>(_vel.Y + Gravity * timeMult, MaxFallSpeed);
				// Moves in short steps, so it stops on the ground instead of sinking into it
				float stepCount = std::max<float>(1.0f, std::ceil(std::abs(_vel.Y * timeMult) / 2.0f));
				Vector2f step = Vector2f(_vel.X * timeMult, _vel.Y * timeMult) / stepCount;
				for (std::int32_t i = 0; i < (std::int32_t)stepCount; i++) {
					MoveInstantly(step, MoveType::Relative | MoveType::Force);
					OnUpdateHitbox();
					if (HasGroundBelow(0.0f)) {
						Land();
						break;
					}
				}
				break;
			}
			case State::Landing: {
				_ticks += ticks;
				if (_ticks >= LandingTicks) {
					// It turns back when too far from home or when it went past the player
					if (auto target = _target.lock()) {
						float playerX = target->GetPos().X;
						if (!IsFacingLeft()) {
							if (_pos.X > _homePos.X + HomeRange || _pos.X > playerX + PastPlayerRange) {
								SetFacingLeft(true);
							}
						} else if (_pos.X < _homePos.X - HomeRange || _pos.X < playerX - PastPlayerRange) {
							SetFacingLeft(false);
						}
					}
					Decide();
				}
				break;
			}
			case State::Spewing: {
				_ticks += ticks;
				if (!_fireballSpawned && _ticks >= SpewFireballTick) {
					_fireballSpawned = true;

					std::shared_ptr<Fireball> fireball = std::make_shared<Fireball>();
					uint8_t fireballParams[2] = { (uint8_t)(IsFacingLeft() ? 1 : 0), (uint8_t)GetDifficultyIndex() };
					fireball->OnActivated(ActorActivationDetails(
						_levelHandler,
						Vector3i((std::int32_t)(_pos.X + (IsFacingLeft() ? -16.0f : 16.0f)), (std::int32_t)(_pos.Y - 5.0f), _renderer.GetLayer() - 2),
						fireballParams
					));
					_levelHandler->AddActor(fireball);
				}
				if (_ticks >= SpewTicks) {
					SetAnimation(AnimState::Idle);
					Decide();
				}
				break;
			}
			case State::TornadoWindUp: {
				_ticks += ticks;
				if (_ticks >= TornadoWindUpTicks) {
					_state = State::TornadoMoving;
					_ticks = 0.0f;
					Vector2f diff = _tornadoTarget - _pos;
					float length = diff.Length();
					_vel = (length > 0.001f ? diff / length * _tornadoSpeed : Vector2f::Zero);
					SetAnimation((AnimState)1073741831);
				}
				break;
			}
			case State::TornadoMoving: {
				_ticks += ticks;
				MoveInstantly(Vector2f(_vel.X * timeMult, _vel.Y * timeMult), MoveType::Relative | MoveType::Force);
				if (_ticks >= _tornadoMoveTicks) {
					_state = State::TornadoStopping;
					_ticks = 0.0f;
					StopTornadoNoise();
					SetTransition((AnimState)1073741832, false);
				}
				break;
			}
			case State::TornadoStopping: {
				_ticks += ticks;
				_vel *= std::pow(0.5f, ticks);
				MoveInstantly(Vector2f(_vel.X * timeMult, _vel.Y * timeMult), MoveType::Relative | MoveType::Force);
				if (_ticks >= TornadoStopTicks) {
					_state = State::Falling;
					_vel = Vector2f::Zero;
					SetAnimation((AnimState)1073741833);
				}
				break;
			}
			default:
				break;
		}

		// Level bounds
		Recti levelBounds = _levelHandler->GetLevelBounds();
		if (_pos.X < levelBounds.X) {
			_pos.X = float(levelBounds.X);
		} else if (_pos.X > levelBounds.X + levelBounds.W) {
			_pos.X = float(levelBounds.X + levelBounds.W);
		}
	}

	void Bubba::OnUpdateHitbox()
	{
		UpdateHitbox(20, 24);
	}

	Vector2f Bubba::GetContactThrowSpeed() const
	{
		// A player hit by the tornado is carried off at its speed
		return (_state == State::TornadoMoving || _state == State::TornadoStopping ? _vel : Vector2f::Zero);
	}

	bool Bubba::OnPerish(ActorBase* collider)
	{
		StopTornadoNoise();

		CreateParticleDebrisOnPerish(ParticleDebrisEffect::Standard, Vector2f::Zero);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		StringView text = _levelHandler->GetLevelText(_endText);
		_levelHandler->ShowLevelText(text);

		return BossBase::OnPerish(collider);
	}

	void Bubba::Decide()
	{
		_ticks = 0.0f;
		_vel = Vector2f::Zero;

		Player* player = FindNearestPlayer(NoticeDistanceSqr);
		if (player == nullptr) {
			StartJump();
			return;
		}

		_target = player->shared_from_this();

		Vector2f diff = player->GetPos() - _pos;
		if (std::abs(diff.Y) < LevelBand) {
			bool isFacing = (diff.X != 0.0f && (diff.X < 0.0f) == IsFacingLeft());
			if (std::abs(diff.X) < FireballRange && isFacing) {
				StartSpew();
				return;
			}
		}

		// Otherwise a jump, or in one of four cases a tornado
		if (Random().Next(0, 4) != 0) {
			StartJump();
		} else {
			StartTornado(player);
		}
	}

	void Bubba::StartJump()
	{
		_state = State::Crouching;
		_ticks = 0.0f;
		_vel = Vector2f(IsFacingLeft() ? -JumpSpeedX : JumpSpeedX, 0.0f);
		SetTransition((AnimState)1073741825, false);
		SetAnimationTicks(CrouchTicks);
	}

	void Bubba::StartSpew()
	{
		_state = State::Spewing;
		_ticks = 0.0f;
		_fireballSpawned = false;
		PlaySfx("Sneeze"_s);
		// The whole spew plays over the state, its last frames after the fireball, as measured in the original
		SetTransition(AnimState::Shoot, false, [this]() {
			if (_state == State::Spewing) {
				SetTransition((AnimState)1073741828, false);
				SetAnimationTicks(SpewTicks - SpewFireballTick);
			}
		});
		SetAnimationTicks(SpewFireballTick);
	}

	void Bubba::StartTornado(Player* target)
	{
		_state = State::TornadoWindUp;
		_ticks = 0.0f;
		_tornadoTarget = target->GetPos();

		float speedPerTick = (float)(GetDifficultyIndex() * 2 + 4);
		_tornadoSpeed = speedPerTick * TicksPerFrame;
		_tornadoMoveTicks = (_tornadoTarget - _pos).Length() / speedPerTick;

		_tornadoNoise = PlaySfx("Tornado"_s);
		SetTransition((AnimState)1073741830, false);
	}

	bool Bubba::HasGroundBelow(float dy) const
	{
		TileCollisionParams params = { TileDestructType::None, true };
		AABBf feet = AABBf(AABBInner.L + 2.0f, AABBInner.B + dy - 1.0f, AABBInner.R - 2.0f, AABBInner.B + dy + 3.0f);
		return !_levelHandler->IsPositionEmpty(const_cast<Bubba*>(this), feet, params);
	}

	void Bubba::Land()
	{
		// Back out of the ground so it stands on top of it
		for (std::int32_t i = 0; i < 12 && HasGroundBelow(0.0f); i++) {
			MoveInstantly(Vector2f(0.0f, -1.0f), MoveType::Relative | MoveType::Force);
			OnUpdateHitbox();
		}

		_state = State::Landing;
		_ticks = 0.0f;
		_vel = Vector2f::Zero;
		SetTransition(AnimState::TransitionFallToIdle, false);
		SetAnimationTicks(LandingTicks);
	}

	void Bubba::StopTornadoNoise()
	{
		if (_tornadoNoise != nullptr) {
			_tornadoNoise->Stop();
			_tornadoNoise = nullptr;
		}
	}

	Task<bool> Bubba::Fireball::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetFacingLeft(details.Params[0] != 0);
		float speedPerTick = (float)(details.Params[1] + 3);
		_speed.X = (IsFacingLeft() ? -speedPerTick : speedPerTick) * TicksPerFrame;
		_timeLeft = Lifetime;

		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);
		CanCollideWithShots = false;

		_health = INT32_MAX;

		async_await RequestMetadataAsync("Boss/Bubba"_s);
		SetAnimation((AnimState)1073741834);

		async_return true;
	}

	void Bubba::Fireball::OnUpdate(float timeMult)
	{
		MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);

		TileCollisionParams params = { TileDestructType::None, true };
		if (_timeLeft <= 0.0f || !_levelHandler->IsPositionEmpty(this, AABBInner, params)) {
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

	bool Bubba::Fireball::OnHandleCollision(ActorBase* other)
	{
		if (auto* player = runtime_cast<Player>(other)) {
			DecreaseHealth(INT32_MAX);
		}

		return ActorBase::OnHandleCollision(other);
	}

	bool Bubba::Fireball::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.GetLayer() + 2), Explosion::Type::RF);

		return EnemyBase::OnPerish(collider);
	}
}
