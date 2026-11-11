#include "Witch.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Explosion.h"

#include "../../../nCine/Base/Random.h"

#include <algorithm>
#include <cmath>
#include <float.h>

namespace Jazz2::Actors::Enemies
{
	Witch::Witch()
		: _attackTime(FirstAttackTicks / TicksPerFrame), _playerHit(false), _awake(false), _orbitPhase(0.0f), _originPos(Vector2f::Zero)
	{
	}

	void Witch::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Witch"_s);
		PreloadMetadataAsync("Interactive/PlayerFrog"_s);
	}

	Task<bool> Witch::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::ApplyGravitation, false);
		_originPos = _pos;

		SetHealthByDifficulty(30);
		_scoreValue = 1000;

		async_await RequestMetadataAsync("Enemy/Witch"_s);
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Witch::OnUpdate(float timeMult)
	{
		OnUpdateHitbox();
		HandleBlinking(timeMult);
		UpdateFrozenState(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);

		if (_playerHit) {
			if (_attackTime > 0.0f) {
				_attackTime -= timeMult;
			} else {
				EnemyBase::OnPerish(nullptr);
			}
			return;
		}

		if (_attackTime > 0.0f) {
			_attackTime -= timeMult;
		}

		// Measured in JJ2+: she hangs still while casting
		if (_currentTransition != nullptr) {
			_speed = Vector2f::Zero;
			return;
		}

		Player* target = nullptr;
		float targetDistanceSqr = FLT_MAX;
		for (auto* player : _levelHandler->GetPlayers()) {
			if (player->GetHealth() <= 0) {
				continue;
			}
			float distanceSqr = (player->GetPos() - _pos).SqrLength();
			if (distanceSqr < targetDistanceSqr) {
				target = player;
				targetDistanceSqr = distanceSqr;
			}
		}
		// She flies along an ellipse around her starting point until a player comes near
		if (!_awake) {
			if (target != nullptr && targetDistanceSqr < WakeDistanceSqr) {
				_awake = true;
				_attackTime = FirstAttackTicks / TicksPerFrame;
			} else {
				_orbitPhase = std::fmod(_orbitPhase + timeMult * TicksPerFrame * (fTwoPi / OrbitPeriodTicks), fTwoPi);
				float orbitSin, orbitCos;
				sincosApprox(_orbitPhase, orbitSin, orbitCos);
				MoveInstantly(Vector2f(_originPos.X + OrbitRadiusX * orbitSin, _originPos.Y + OrbitRadiusY * orbitCos),
					MoveType::Absolute | MoveType::Force);
				SetFacingLeft(_orbitPhase >= fPi);
				_speed = Vector2f::Zero;
				return;
			}
		}

		if (target == nullptr) {
			_speed = Vector2f::Zero;
			return;
		}

		// She never flies lower than a fixed distance below her starting point, and doesn't cast while held there
		bool heldBack = (_pos.Y > _originPos.Y + MaxDropBelowOrigin);
		if (heldBack) {
			MoveInstantly(Vector2f(_pos.X, _originPos.Y + MaxDropBelowOrigin), MoveType::Absolute | MoveType::Force);
		}

		if (!heldBack && _attackTime <= 0.0f && targetDistanceSqr < AttackDistance * AttackDistance) {
			_attackTime = AttackTicks / TicksPerFrame;
			_speed = Vector2f::Zero;

			PlaySfx("MagicFire"_s);

			SetTransition(AnimState::TransitionAttack, true, [this]() {
				Vector2f bulletPos = Vector2f(_pos.X + (IsFacingLeft() ? -24.0f : 24.0f), _pos.Y);

				std::shared_ptr<MagicBullet> magicBullet = std::make_shared<MagicBullet>(std::static_pointer_cast<Witch>(shared_from_this()));
				magicBullet->OnActivated(ActorActivationDetails(
					_levelHandler,
					Vector3i((std::int32_t)bulletPos.X, (std::int32_t)bulletPos.Y, _renderer.GetLayer() + 1)
				));
				_levelHandler->AddActor(magicBullet);

				Explosion::Create(_levelHandler, Vector3i((std::int32_t)bulletPos.X, (std::int32_t)bulletPos.Y, _renderer.GetLayer() + 2), Explosion::Type::TinyDark);
			});
			return;
		}

		// The target point above the player sways sideways slowly with the level time, which is her drift
		float swayPhase = std::fmod(_levelHandler->GetElapsedFrames() * TicksPerFrame, SwayPeriodTicks) * (fTwoPi / SwayPeriodTicks);
		Vector2f diff = target->GetPos() - _pos;
		diff.X += SwayAmplitude * sinApprox(swayPhase);
		diff.Y -= FlyAboveDistance;
		// Each original tick her speed becomes a quarter of its sum with the clamped offset, so it settles at a third of it
		Vector2f targetSpeed = Vector2f(std::clamp(diff.X, -FlyClampX, FlyClampX), std::clamp(diff.Y, -FlyClampY, FlyClampY)) * (TicksPerFrame / 3.0f);
		float keep = powf(0.25f, timeMult * TicksPerFrame);
		_speed = targetSpeed + (_speed - targetSpeed) * keep;

		if (_speed.X != 0.0f) {
			SetFacingLeft(_speed.X < 0.0f);
		}
	}

	void Witch::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteValueAsLE<float>(_attackTime);
		dest.WriteValue<std::uint8_t>(_playerHit ? 1 : 0);
	}

	void Witch::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		_attackTime = src.ReadValueAsLE<float>();
		_playerHit = (src.ReadValue<std::uint8_t>() != 0);
		_awake = true;
	}

	void Witch::OnUpdateHitbox()
	{
		UpdateHitbox(30, 30);
	}

	bool Witch::OnPerish(ActorBase* collider)
	{
		// It must be done here, because the player may not exist after animation callback 
		AddScoreToCollider(collider);

		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		SetTransition(AnimState::TransitionDeath, false, [this, collider]() {
			EnemyBase::OnPerish(collider);
		});

		CreateParticleDebrisOnPerish(collider);

		return false;
	}

	bool Witch::OnTileDeactivated()
	{
		// Witch cannot be deactivated
		return false;
	}

	void Witch::OnPlayerHit()
	{
		_playerHit = true;
		_attackTime = 400.0f;

		_speed.X = (IsFacingLeft() ? -9.0f : 9.0f);
		_speed.Y = -0.8f;

		PlaySfx("Laugh"_s);
	}

	Task<bool> Witch::MagicBullet::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::IsInvulnerable | ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		_health = INT32_MAX;

		async_await RequestMetadataAsync("Enemy/Witch"_s);
		SetAnimation((AnimState)1073741828);

		async_return true;
	}

	void Witch::MagicBullet::OnUpdate(float timeMult)
	{
		MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);
		OnUpdateHitbox();

		if (_time <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		} else {
			_time -= timeMult;
			FollowNearestPlayer();
		}
	}

	void Witch::MagicBullet::OnUpdateHitbox()
	{
		UpdateHitbox(10, 10);
	}

	bool Witch::MagicBullet::OnHandleCollision(ActorBase* other)
	{
		if (auto* player = runtime_cast<Player>(other)) {
			// An invulnerable player isn't turned into a frog, as in JJ2+
			if (player->IsInvulnerable()) {
				return false;
			}
			DecreaseHealth(INT32_MAX);
			if (auto owner = _owner.lock()) {
				owner->OnPlayerHit();
			}

			player->MorphTo(PlayerType::Frog);
			return true;
		}

		return false;
	}

	void Witch::MagicBullet::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = 0.7f;
		light.Brightness = 0.4f;
		light.RadiusNear = 0.0f;
		light.RadiusFar = 30.0f;
	}

	void Witch::MagicBullet::FollowNearestPlayer()
	{
		bool found = false;
		Vector2f targetPos;

		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			if (player->GetHealth() <= 0) {
				continue;
			}

			Vector2f newPos = player->GetPos();
			if (!found || (_pos - newPos).SqrLength() < (_pos - targetPos).SqrLength()) {
				targetPos = newPos;
				found = true;
			}
		}

		if (found) {
			Vector2f diff = (targetPos - _pos).Normalized();
			Vector2f speed = (_speed + diff * 0.8f).Normalized();
			_speed = speed * 5.0f;
			_renderer.SetRotation(atan2Approx(_speed.Y, _speed.X));
		}
	}
}