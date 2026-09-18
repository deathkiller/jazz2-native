#include "RFShot.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"
#include "../Explosion.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	RFShot::RFShot()
		: _fired(0), _smokeTimer(3.0f), _blastDelayLeft(BlastDelayUnarmed)
	{
	}

	Task<bool> RFShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];
		_strength = 2;

		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/RF"_s);

		AnimState state = AnimState::Idle;
		if ((_upgrades & 0x1) != 0) {
			_timeLeft = 24;
			state |= (AnimState)1;
		} else {
			_timeLeft = 30;
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

		// Player::FireWeaponRF() spawns this at the player's leading edge, which is where the original's
		// flight times say its shot starts - but a shot detonates when its own *box* meets the wall, not
		// when its centre does, so spawning the centre there puts the leading edge half a shot too far
		// forward. Measured, that was a flat three ticks at every range: the slope was already right at
		// 3 px/tick and each reading came in early by the same amount. Backing the centre off by half the
		// shot's own width is what the geometry asks for - travel is then `wall - pos - halfShot`, and that
		// equals the player-edge-to-wall gap the rule is written in.
		if (!_levelHandler->IsReforged()) {
			float halfShot = (AABBInner.R - AABBInner.L) * 0.5f;
			MoveInstantly(Vector2f(_pos.X + (isFacingLeft ? halfShot : -halfShot), _pos.Y),
				MoveType::Absolute | MoveType::Force);
		}

		float angleRel = angle * (isFacingLeft ? -1 : 1);

		// The original's RF is half as fast as this engine's, which is most of why firing one into a wall is
		// a way to launch yourself there and hardly one here: ours detonated before the player had left the
		// ground. The upgraded variant keeps its ratio to the plain one, since only the plain one was
		// measured and the ratio is the part that is not being contradicted.
		float baseSpeed = ((_upgrades & 0x1) != 0 ? 6.4f : 6.0f);
		if (!_levelHandler->IsReforged()) {
			baseSpeed *= LegacyFlightSpeed / 6.0f;
		}
		if (isFacingLeft) {
			_speed.X = std::min(0.0f, speed.X) - cosf(angleRel) * baseSpeed;
		} else {
			_speed.X = std::max(0.0f, speed.X) + cosf(angleRel) * baseSpeed;
		}
		_speed.Y = sinf(angleRel) * baseSpeed;

		_renderer.setRotation(angle);
		_renderer.setDrawEnabled(false);
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
		} else {
			Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() + 2), Explosion::Type::TinyBlue);
			_smokeTimer = 6.0f;
		}

		_fired++;
		if (_fired == 2) {
			// The snap to the gunspot is a *rendering* trick - the shot is hidden for two frames and then
			// placed at the muzzle - and it costs the flight its first two frames plus however far the
			// gunspot is ahead of where the shot started. Non-Reforged is spawned at the player's leading
			// edge instead (see Player::FireWeaponRF), which is where the original's flight times say its
			// shot begins, so moving it afterwards would undo exactly that. Only the reveal is kept.
			if (_levelHandler->IsReforged()) {
				MoveInstantly(_gunspotPos, MoveType::Absolute | MoveType::Force);
			}
			_renderer.setDrawEnabled(true);
		}
	}

	void RFShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_fired >= 2) {
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

		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.layer() + 2),
			(_upgrades & 0x1) != 0 ? Explosion::Type::RFUpgraded : Explosion::Type::RF);

		PlaySfx("Explode"_s, 0.6f);

		return ShotBase::OnPerish(collider);
	}

	void RFShot::OnHitWall(float timeMult)
	{
		// The original's blast does not land on the tick the shot arrives - see LegacyBlastDelay. Lighting
		// the fuse rather than detonating is what puts the knockback where it is measured: point blank the
		// flight is now 9 ticks against 9. `BlastDelayUnarmed` keeps a second wall report from restarting it.
		//
		// What this does NOT fix is the flight at range, and the remaining error is a starting position
		// rather than a speed: the shot spends two frames hidden and is then teleported to the gunspot,
		// which for any gap under ~24 px is already at or past the wall. So the distance it flies is neither
		// the gap nor proportional to it, and the flight comes out flat at 12 ticks where the original grows
		// 13, 16, 17. The original behaves as though its shot starts at the player's front edge and covers
		// exactly the gap. Pinning ours to the same place needs the shot's own position in the trace, which
		// neither probe logs. It is also why `wb_rf_r48` still throws a player the original leaves alone -
		// the shot dies short of the wall, so ApplyBlastKnockback() is handed a distance inside its reach.
		if (!_levelHandler->IsReforged() && _blastDelayLeft == BlastDelayUnarmed) {
			_blastDelayLeft = LegacyBlastDelay;
			_speed = Vector2f::Zero;
			_externalForce = Vector2f::Zero;
			return;
		}

		DecreaseHealth(INT32_MAX);
	}

	void RFShot::OnRicochet()
	{
	}
}