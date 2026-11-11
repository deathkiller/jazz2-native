#include "RFShot.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"
#include "../Explosion.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	RFShot::RFShot()
		: _smokeTimer(3.0f), _blastDelayLeft(BlastDelayUnarmed), _hiddenLeft(0.0f), _spread(0.0f), _fired(0)
	{
	}

	Task<bool> RFShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];
		_strength = 2;
		_ricochetLimit = MaxTileRicochets;

		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/RF"_s);

		AnimState state = AnimState::Idle;
		if ((_upgrades & 0x1) != 0) {
			_timeLeft = 35.0f * TicksToFrames;
			state |= (AnimState)1;
		} else {
			_timeLeft = 40.0f * TicksToFrames;
		}

		SetAnimation(state);
		PlaySfx("Fire"_s, 0.4f);

		async_return true;
	}

	void RFShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);
		float sinAngle, cosAngle;
		sincosApprox(angleRel, sinAngle, cosAngle);
		bool up = (std::abs(sinAngle) > 0.99f);

		// A shot detonates when its box meets the wall, the original's when its centre does, so the centre starts half a shot back
		if (!up) {
			float halfShot = (AABBInner.R - AABBInner.L) * 0.5f;
			MoveInstantly(Vector2f(_pos.X + (isFacingLeft ? halfShot : -halfShot), _pos.Y), MoveType::Absolute | MoveType::Force);
		}

		float baseSpeed = ((_upgrades & 0x1) != 0 ? 1.5f : 1.0f) * TickSpeedScale;
		Vector2f dir(isFacingLeft ? -cosAngle : cosAngle, sinAngle);
		float spreadSpeed = _spread * TickSpeedScale;
		_speed = dir * baseSpeed + Vector2f(-sinAngle * spreadSpeed, cosAngle * spreadSpeed);
		// The player's whole speed adds to the rocket's, it isn't capped or fading
		if (!up) {
			_speed.X += speed.X;
		}
		SetAcceleration(dir, 0.1875f * TickAccelScale, 0.0f);
		ShortenUpShotLifetime(sinAngle);

		_renderer.SetRotation(angle);
		_renderer.SetDrawEnabled(false);
		_hiddenLeft = HiddenTicks * TicksToFrames;
	}

	void RFShot::OnUpdate(float timeMult)
	{
		// Once the fuse is lit the shot is parked where it struck and simply waits, so it neither moves nor
		// re-enters OnHitWall(). Only the wait is measured: a shot that reaches the end of its own lifetime
		// without hitting anything still goes off the way it always did, since nothing measures that case.
		if (_blastDelayLeft > 0.0f) {
			_blastDelayLeft -= timeMult;
			if (_blastDelayLeft <= 0.0f) {
				_blastDelayLeft = 0.0f;
				DecreaseHealth(INT32_MAX);
			}
			return;
		}

		std::int32_t n = GetMovementSubstepCount(timeMult);
		TileCollisionParams params = { TileDestructType::Weapon, false, WeaponType::RF, _strength };
		for (std::int32_t i = 0; i < n && params.WeaponStrength > 0; i++) {
			TryMovement(timeMult / n, params);
		}
		if (params.TilesDestroyed > 0) {
			if (auto* player = runtime_cast<Player>(_owner.get())) {
				player->AddScore(params.TilesDestroyed * 50);
			}
		}
		if (params.WeaponStrength <= 0) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		ShotBase::OnUpdate(timeMult);

		if (_smokeTimer > 0.0f) {
			_smokeTimer -= timeMult;
		} else if (_renderer.IsDrawEnabled()) {
			// No trail while the rocket itself is still hidden
			Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() + 2), Explosion::Type::TinyBlue);
			_smokeTimer = 6.0f;
		}

		if (_fired < 255) {
			_fired++;
		}
		if (_hiddenLeft > 0.0f) {
			_hiddenLeft -= timeMult;
		}
		if (_hiddenLeft <= 0.0f) {
			RevealPastMuzzle(_gunspotPos);
		}
	}

	void RFShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_fired >= 2 && _renderer.IsDrawEnabled()) {
			auto& light1 = lights.emplace_back();
			light1.Pos = _pos;
			light1.Intensity = 0.4f;
			light1.Brightness = 0.2f;
			light1.RadiusNear = 0.0f;
			light1.RadiusFar = 60.0f;

			auto& light2 = lights.emplace_back();
			light2.Pos = _pos;
			light2.Intensity = 0.8f;
			light2.Brightness = 0.8f;
			light2.RadiusNear = 3.0f;
			light2.RadiusFar = 14.0f;

		}
	}

	bool RFShot::OnPerish(ActorBase* collider)
	{
		_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, 36.0f, [this](ActorBase* actor) {
			if (auto* player = runtime_cast<Player>(actor)) {
				// Fired point blank into a wall the player is touching, the shot ends up within a pixel of
				// them and can easily come to rest marginally behind - so the side it went off on is noise,
				// and taking it literally throws the player into the wall instead of off it. Which way the
				// shot was travelling is what decides it there.
				float dx = _pos.X - player->GetPos().X;
				float dy = _pos.Y - player->GetPos().Y;
				bool pushLeft = (std::abs(dx) > 4.0f ? dx > 0.0f : !IsFacingLeft());
				// The search above tests the player's box rather than their centre, so it reaches further
				// than the original does - the player decides whether it was actually close enough
				if (player->ApplyBlastKnockback(pushLeft, dx * dx + dy * dy)) {
					_levelHandler->HandlePlayerPushed(player);
				}
			}
			return true;
		});

		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.GetLayer() + 2),
			(_upgrades & 0x1) != 0 ? Explosion::Type::RFUpgraded : Explosion::Type::RF);

		PlaySfx("Explode"_s, 0.6f);

		return ShotBase::OnPerish(collider);
	}

	void RFShot::OnHitWall(float timeMult)
	{
		auto events = _levelHandler->EventMap();
		if (events != nullptr) {
			std::uint8_t* eventParams;
			if (events->GetEventByPosition(_pos.X + _speed.X, _pos.Y + _speed.Y, &eventParams) == EventType::ModifierRicochet) {
				TriggerRicochet(nullptr);
				return;
			}
		}

		// The blast lands a tick after the rocket meets the wall and what it throws moves a tick after that
		if (_blastDelayLeft == BlastDelayUnarmed) {
			_blastDelayLeft = BlastDelayTicks * TicksToFrames;
			_speed = Vector2f::Zero;
			_acceleration = 0.0f;
			_externalForce = Vector2f::Zero;
			return;
		}

		DecreaseHealth(INT32_MAX);
	}

	void RFShot::OnRicochet()
	{
		OnRicochetExact();

		_renderer.SetRotation(atan2Approx(_speed.Y, _speed.X));
	}
}