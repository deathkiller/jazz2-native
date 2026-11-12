#include "RollingRock.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Environment
{
	namespace
	{
		// The original's rules, in its ticks and px per tick
		constexpr float ShakeTicks = 40.0f;
		constexpr float LaunchSpeedScale = 0.5f;
		constexpr float RollAcceleration = 1.0f / 16.0f;
		constexpr float MaxRollSpeed = 3.0f;
		constexpr float Gravity = 0.125f;
		constexpr float StopSpeed = 0.5f;
		constexpr float HurtSpeed = 2.0f;
		constexpr float LoudBounceSpeed = 2.0f;
		constexpr float QuakeDistance = 400.0f;
		constexpr float RockReach = 74.0f;
		constexpr float PushSpeed = 1.0f;
		constexpr float AngleUnit = fTwoPi / 1024.0f;
		constexpr float SpawnOffsetY = 12.0f;
		constexpr float ContactHalfWidth = 40.0f;
		constexpr float ContactRadius = 60.0f;
		// Only until the player's next update, so a contact on every 3rd tick isn't a steady push (Reforged shows one)
		constexpr float PushHoldFrames = 1.0f;
		constexpr float ReforgedPushHoldFrames = 6.0f;
	}

	RollingRock::RollingRock()
		: _triggerSpeedX(0.0f), _triggerSpeedY(0.0f), _accelerationX(0.0f), _time(0.0f), _tickTime(0.0f), _spinBoost(4.0f), _id(0),
			_rockState(RockState::Idle)
	{
	}

	RollingRock::~RollingRock()
	{
		for (auto* player : _levelHandler->GetPlayers()) {
			player->CancelCarryingObject(this);
		}
	}

	void RollingRock::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/RollingRock"_s);
	}

	Task<bool> RollingRock::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_renderer.SetLayer(_renderer.GetLayer() + 10);

		EventParamsReader params(details);
		_id = params.GetUint8(0);
		_triggerSpeedX = (float)params.GetInt8(1);
		_triggerSpeedY = (float)params.GetInt8(2);

		// The player stands on its round top and pushes its sides, see HandlePlayerContact(), it isn't a solid box
		SetState(ActorState::CollideWithSolidObjects | ActorState::CollideWithSolidObjectsBelow | ActorState::IsInvulnerable, true);
		SetState(ActorState::ApplyGravitation, false);
		_canHurtPlayer = false;
		// Measured in JJ2+: it sits 12 px lower than other objects
		_pos.Y += SpawnOffsetY;

		async_await RequestMetadataAsync("Object/RollingRock"_s);
		SetAnimation(AnimState::Idle);

		_renderer.SetRotation(Random().Next(0, 1024) * AngleUnit);

		async_return true;
	}

	void RollingRock::OnUpdate(float timeMult)
	{
		UpdateFrozenState(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		_tickTime += timeMult * TicksPerFrame;
		while (_tickTime >= 1.0f) {
			_tickTime -= 1.0f;
			AdvanceTick();
		}

		// Only a fast rock hurts, as in the original
		_canHurtPlayer = (_rockState == RockState::Rolling && std::abs(_speedPerTick.X) + std::abs(_speedPerTick.Y) > HurtSpeed);
	}

	void RollingRock::AdvanceTick()
	{
		switch (_rockState) {
			case RockState::Shaking: {
				// Measured in JJ2+: it wobbles around where it stood before it goes
				_time += 1.0f;
				std::int32_t tick = (std::int32_t)_time + 5;
				MoveInstantly(Vector2f(sinf((tick & 15) * fTwoPi / 16.0f), -sinf((tick & 31) * fTwoPi / 32.0f)), MoveType::Relative | MoveType::Force);
				_renderer.SetRotation(_renderer.GetRotation() + Random().Next(-3, 5) * AngleUnit);
				if (_time >= ShakeTicks) {
					StartRolling(_triggerSpeedX * LaunchSpeedScale, _triggerSpeedY * LaunchSpeedScale,
						_triggerSpeedX < 0.0f ? -RollAcceleration : RollAcceleration);
				}
				break;
			}
			case RockState::Rolling: {
				Roll();
				break;
			}
			default: break;
		}

		HandlePlayerContact();
	}

	void RollingRock::HandlePlayerContact()
	{
		for (auto* player : _levelHandler->GetPlayers()) {
			float dx = std::trunc(player->_pos.X - _pos.X);
			float dy = std::trunc(player->_pos.Y - _pos.Y);
			bool standing = false;
			if (std::abs(dx) < ContactHalfWidth && dx * dx + dy * dy < ContactRadius * ContactRadius) {
				if (dy <= -21.0f) {
					// The top is a dome to stand on, the rock doesn't carry the player along
					float top = _pos.Y - 20.0f - 40.0f * cosf(dx * fPi / 80.0f);
					if (player->_pos.Y >= top - 1.0f && player->_pos.Y < top + 7.0f && player->_speed.Y >= 0.0f) {
						player->MoveInstantly(Vector2f(player->_pos.X, top), MoveType::Absolute | MoveType::Force);
						player->_speed.Y = 0.0f;
						player->UpdateCarryingObject(this);
						standing = true;
					}
				} else if (dy >= -19.0f) {
					PushBySide(player, dx);
				}
			}
			if (!standing) {
				player->CancelCarryingObject(this);
			}
		}
	}

	void RollingRock::PushBySide(Player* player, float dx)
	{
		// Pushing against it (or just standing still) halves the player's speed and kicks the rock away
		if (dx == 0.0f) {
			return;
		}
		bool fromLeft = (dx < 0.0f);
		bool pushing = (fromLeft ? player->_speed.X >= 0.0f : player->_speed.X <= 0.0f);
		if (pushing) {
			player->_speed.X *= 0.5f;
			player->_pushObjectHoldTime = (_levelHandler->IsReforged() ? ReforgedPushHoldFrames : PushHoldFrames);
			if (fromLeft ? _speedPerTick.X <= 0.0f : _speedPerTick.X >= 0.0f) {
				// The kick keeps whatever acceleration the rock had left
				if (_rockState != RockState::Rolling) {
					StartRolling(_speedPerTick.X, _speedPerTick.Y, _accelerationX);
				}
				if (fromLeft) {
					_speedPerTick.X += PushSpeed;
				} else {
					_speedPerTick.X = -PushSpeed;
				}
			}
		}

		// The player is eased out of the rock, an eighth of the way each tick
		float targetX = _pos.X + (fromLeft ? -ContactHalfWidth : ContactHalfWidth);
		player->MoveInstantly(Vector2f((targetX - player->_pos.X) * 0.125f, 0.0f), MoveType::Relative);
	}

	void RollingRock::StartRolling(float speedX, float speedY, float accelerationX)
	{
		_rockState = RockState::Rolling;
		_speedPerTick = Vector2f(speedX, speedY);
		_accelerationX = accelerationX;
	}

	void RollingRock::Roll()
	{
		// It moves first and speeds up after, there's no friction
		MoveInstantly(_speedPerTick, MoveType::Relative | MoveType::Force);
		_speedPerTick.X = std::clamp(_speedPerTick.X + _accelerationX, -MaxRollSpeed, MaxRollSpeed);

		// Only the probes the speed can use, one-way tiles are tested by the fall direction before the gravity
		bool downwards = (_speedPerTick.Y >= 0.0f);
		bool up = (downwards || IsPositionEmpty(_pos.X, _pos.Y - 33.0f, downwards));
		bool down = IsPositionEmpty(_pos.X - 4.0f, _pos.Y + 36.0f, downwards) && IsPositionEmpty(_pos.X + 4.0f, _pos.Y + 36.0f, downwards);

		if (down) {
			_speedPerTick.Y += (_pos.Y > _levelHandler->GetWaterLevel() ? Gravity * 0.25f : Gravity);
		}

		bool hit = false;
		if ((_speedPerTick.Y < 0.0f && !up) || (_speedPerTick.Y > 0.0f && !down)) {
			// Back out of the floor or the ceiling, then bounce at half the speed
			MoveInstantly(Vector2f(0.0f, -_speedPerTick.Y), MoveType::Relative | MoveType::Force);
			_speedPerTick.Y *= -0.5f;
			hit = true;
			// The sides are checked from there, otherwise a hard landing reads the floor as a wall
			downwards = (_speedPerTick.Y >= 0.0f);
		}
		if (_speedPerTick.X != 0.0f && !IsSideEmpty(_speedPerTick.X < 0.0f ? -1.0f : 1.0f, downwards)) {
			// Backed out too, or pushing it against a wall again and again would walk it into the wall
			MoveInstantly(Vector2f(-_speedPerTick.X, 0.0f), MoveType::Relative | MoveType::Force);
			_speedPerTick.X *= -0.5f;
			_accelerationX *= -0.5f;
			hit = true;
		}

		if (hit) {
			_spinBoost = (std::abs(_speedPerTick.Y) > 1.5f ? 16.0f : 8.0f);
			ShakeNearbyCameras(4096.0f, 8.0f * (std::abs(_speedPerTick.X) + std::abs(_speedPerTick.Y)));
			if (std::abs(_speedPerTick.X) > LoudBounceSpeed || std::abs(_speedPerTick.Y) > LoudBounceSpeed) {
				float gain = std::min((std::abs(_speedPerTick.X) * 4.0f + std::abs(_speedPerTick.Y) * 8.0f) / 64.0f, 1.0f);
				PlaySfx("Hit"_s, gain);
			}
		}

		_renderer.SetRotation(_renderer.GetRotation() + _spinBoost * _speedPerTick.X * 0.5f * AngleUnit);
		_spinBoost = std::max(_spinBoost - 1.0f, 3.0f);

		HitOtherRocks();

		// It stops for good as soon as it rolls slowly, even in the air
		if (std::abs(_speedPerTick.X) < StopSpeed) {
			// The acceleration stays, a later kick picks it up again
			_speedPerTick = Vector2f::Zero;
			MoveInstantly(Vector2f(std::round(_pos.X), std::round(_pos.Y)), MoveType::Absolute | MoveType::Force);
			_rockState = RockState::Done;
		}
	}

	void RollingRock::HitOtherRocks()
	{
		_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, RockReach, [this](ActorBase* actor) {
			auto* other = runtime_cast<RollingRock>(actor);
			if (other == nullptr || other == this || other->_rockState == RockState::Shaking) {
				return true;
			}
			float dx = other->_pos.X - _pos.X;
			if ((other->_pos - _pos).SqrLength() >= RockReach * RockReach) {
				return true;
			}

			// Both roll apart at their average speed
			float speed = (std::abs(_speedPerTick.X) + std::abs(other->_speedPerTick.X)) * 0.5f;
			_speedPerTick.X = (dx > 0.0f ? -speed : speed);
			_accelerationX = std::copysign(_accelerationX, _speedPerTick.X);
			if (other->_rockState == RockState::Rolling || speed > StopSpeed) {
				if (other->_rockState != RockState::Rolling) {
					other->StartRolling(0.0f, 0.0f, 0.0f);
				}
				other->_speedPerTick.X = (dx > 0.0f ? speed : -speed);
				other->_accelerationX = std::copysign(other->_accelerationX, other->_speedPerTick.X);
			}
			return true;
		});
	}

	void RollingRock::ShakeNearbyCameras(float falloff, float maxTicks)
	{
		for (auto* player : _levelHandler->GetPlayers()) {
			float distanceSqr = (player->GetPos() - _pos).SqrLength();
			if (distanceSqr < QuakeDistance * QuakeDistance) {
				float ticks = std::min((QuakeDistance * QuakeDistance - distanceSqr) / falloff, maxTicks);
				if (ticks >= 1.0f) {
					_levelHandler->ShakeCameraView(player, ticks / TicksPerFrame);
				}
			}
		}
	}

	void RollingRock::OnUpdateHitbox()
	{
		UpdateHitbox(50, 50);
	}

	void RollingRock::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteValue<std::uint8_t>((std::uint8_t)_rockState);
		dest.WriteValueAsLE<float>(_time);
		dest.WriteValueAsLE<float>(_speedPerTick.X);
		dest.WriteValueAsLE<float>(_speedPerTick.Y);
		dest.WriteValueAsLE<float>(_accelerationX);
	}

	void RollingRock::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		std::uint8_t state = src.ReadValue<std::uint8_t>();
		_rockState = (state <= (std::uint8_t)RockState::Done ? (RockState)state : RockState::Idle);
		_time = src.ReadValueAsLE<float>();
		_speedPerTick.X = src.ReadValueAsLE<float>();
		_speedPerTick.Y = src.ReadValueAsLE<float>();
		_accelerationX = src.ReadValueAsLE<float>();
	}

	void RollingRock::OnTriggeredEvent(EventType eventType, std::uint8_t* eventParams)
	{
		if (_rockState == RockState::Idle && eventType == EventType::RollingRockTrigger && eventParams[0] == _id) {
			_rockState = RockState::Shaking;
			_time = 0.0f;
			ShakeNearbyCameras(2048.0f, 40.0f);
		}
	}

	bool RollingRock::IsPositionEmpty(float x, float y, bool downwards)
	{
		// A single pixel, as in the original
		TileCollisionParams params = { TileDestructType::None, downwards };
		float px = std::floor(x), py = std::floor(y);
		return _levelHandler->IsPositionEmpty(this, AABBf(px, py, px, py), params);
	}

	bool RollingRock::IsSideEmpty(float dir, bool downwards)
	{
		return (IsPositionEmpty(_pos.X + dir * 36.0f, _pos.Y, downwards) &&
			IsPositionEmpty(_pos.X + dir * 19.0f, _pos.Y + 19.0f, downwards) &&
			IsPositionEmpty(_pos.X + dir * 4.0f, _pos.Y + 33.0f, downwards));
	}
}
