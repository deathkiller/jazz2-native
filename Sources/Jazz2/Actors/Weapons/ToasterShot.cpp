#include "ToasterShot.h"
#include "../Player.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Base/Random.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	ToasterShot::ToasterShot()
		: _crossAccel(0.0f), _followOffsetY(0.0f), _followLeft(0.0f), _hiddenLeft(0.0f), _fired(0), _vertical(false), _burning(false)
	{
	}

	Task<bool> ToasterShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_strength = 1;
		_upgrades = details.Params[0];

		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/Toaster"_s);

		AnimState state = AnimState::Idle;
		if ((_upgrades & 0x01) != 0) {
			state |= (AnimState)1;
		}

		SetAnimation(state);

		// A flame lasts as long as its animation
		std::int32_t frameCount = (_currentAnimation != nullptr ? _currentAnimation->FrameCount : 0);
		_timeLeft = (frameCount > 0 ? float(frameCount) * TicksPerAnimFrame : 35.0f) * TicksToFrames;

		async_return true;
	}

	void ToasterShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float ax, ay;
		sincosApprox(angle, ay, ax);

		// Every fast fire pickup makes the flame a little faster
		std::int32_t fastFire = 0;
		if (auto* player = runtime_cast<Player>(owner.get())) {
			fastFire = (player->GetWeaponUpgrades()[(std::int32_t)WeaponType::Blaster] >> 1);
		}
		float fireInterval = std::max(Player::FireMinHoldTicks, Player::FireBaseHoldTicks - float(fastFire) * Player::FireFastFireStepTicks);
		float baseSpeed = (99.0f - fireInterval) / 32.0f * TickSpeedScale;

		Vector2f dir(isFacingLeft ? -ax : ax, isFacingLeft ? -ay : ay);
		_speed = dir * baseSpeed;
		_vertical = (std::abs(ay) > 0.99f);
		if (!_vertical) {
			SetCarriedSpeed(speed.X, false);
		}

		// The flame drifts sideways of its path by a small random acceleration
		_crossAccel = float(std::int32_t(Random().Next() % 4) - 1) * Damping * TickAccelScale;

		_followOffsetY = std::clamp(owner->GetPos().Y - _pos.Y, -MaxFollowOffset, MaxFollowOffset);
		_followLeft = FollowTicks * TicksToFrames;
		_hiddenLeft = FollowTicks * TicksToFrames;

		_renderer.SetDrawEnabled(false);
	}

	void ToasterShot::OnUpdate(float timeMult)
	{
		OnUpdateHitbox();

		if (_pos.Y >= _levelHandler->GetWaterLevel()) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		Vector2f step = Vector2f::Zero;
		if (!_burning) {
			float damping = Damping * TickAccelScale * timeMult;
			float maxSpeed = MaxSpeed * TickSpeedScale;
			auto fade = [damping, maxSpeed](float value) {
				if (value > maxSpeed) { return maxSpeed; }
				if (value < -maxSpeed) { return -maxSpeed; }
				if (value > damping) { return value - damping; }
				if (value < -damping) { return value + damping; }
				return 0.0f;
			};
			if (_vertical) {
				_speed.Y = fade(_speed.Y);
				_speed.X += _crossAccel * timeMult;
			} else {
				_speed.X = fade(_speed.X);
				_speed.Y += _crossAccel * timeMult;
			}
			step = Vector2f((_speed.X + (_vertical ? 0.0f : _carriedSpeedX)) * timeMult, _speed.Y * timeMult);
		}

		auto tiles = _levelHandler->TileMap();
		TileCollisionParams params = { TileDestructType::Weapon, _speed.Y >= 0.0f, WeaponType::Toaster, _strength };
		if (tiles == nullptr || tiles->IsTileEmpty(AABBInner, params)) {
			MoveInstantly(step, MoveType::Relative | MoveType::Force, params);
		} else {
			MoveInstantly(step, MoveType::Relative | MoveType::Force, params);
			MoveInstantly(Vector2f(-step.X, -step.Y), MoveType::Relative | MoveType::Force, params);

			if ((_upgrades & 0x1) == 0) {
				DecreaseHealth(INT32_MAX);
			} else if (!_burning) {
				// A powered-up flame stays where it hit and burns for two loops of its animation
				_burning = true;
				_speed = Vector2f::Zero;
				_carriedSpeedX = 0.0f;
				std::int32_t frameCount = (_currentAnimation != nullptr ? _currentAnimation->FrameCount : 0);
				if (frameCount > 0) {
					_timeLeft = std::min(_timeLeft, 2.0f * float(frameCount) * TicksPerAnimFrame * TicksToFrames);
				}
			}
		}
		if (params.TilesDestroyed > 0) {
			if (auto* player = runtime_cast<Player>(_owner.get())) {
				player->AddScore(params.TilesDestroyed * 50);
			}
		}

		if (_followLeft > 0.0f && _owner != nullptr) {
			_followLeft -= timeMult;
			MoveInstantly(Vector2f(_pos.X, _owner->GetPos().Y - _followOffsetY), MoveType::Absolute | MoveType::Force);
		}

		if (_fired < 255) {
			_fired++;
		}
		if (_hiddenLeft > 0.0f) {
			_hiddenLeft -= timeMult;
			if (_hiddenLeft <= 0.0f) {
				_renderer.SetDrawEnabled(true);
			}
		}

		ShotBase::OnUpdate(timeMult);
	}

	void ToasterShot::OnUpdateHitbox()
	{
		AABBInner = AABBf(_pos.X - 3, _pos.Y - 3, _pos.X + 3, _pos.Y + 3);
	}

	void ToasterShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_fired >= 2) {
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

	void ToasterShot::OnRicochet()
	{
		// The remainder is unsigned, so the offset has to be taken from it as a signed number: left unsigned, any
		// value below 50 wrapped round to four billion and the flame left with a speed of that order
		_speed.Y = _speed.Y * -0.2f * (std::int32_t(Random().Next() % 100) - 50);
		_speed.X = _speed.X * -0.2f + (std::int32_t(Random().Next() % 100) - 50) * 0.02f;
	}
}