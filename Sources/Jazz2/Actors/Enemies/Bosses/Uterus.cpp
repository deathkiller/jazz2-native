#include "Uterus.h"
#include "../../../ILevelHandler.h"
#include "../../Player.h"
#include "../../Explosion.h"
#include "../../Weapons/ShotBase.h"

#include "../../../../nCine/Base/Random.h"

#include <cmath>

namespace Jazz2::Actors::Bosses
{
	Uterus::Uterus()
		: _state(StateWaiting), _endText(0), _shieldCount(0), _phase(0.0f), _angle(0.0f), _crabTicks(0.0f), _hasShield(false),
			_touchable(false)
	{
	}

	Uterus::~Uterus()
	{
		for (std::int32_t i = 0; i < MaxShieldPartCount; i++) {
			if (_shields[i] != nullptr) {
				_shields[i]->SetState(ActorState::IsDestroyed, true);
			}
		}
	}

	void Uterus::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Boss/Uterus"_s);
		PreloadMetadataAsync("Enemy/Crab"_s);
	}

	Task<bool> Uterus::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_endText = details.Params[1];
		_originPos = _pos;
		_lastPos = _pos;

		SetState(ActorState::SkipPerPixelCollisions | ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);

		_scoreValue = 3000;

		async_await RequestMetadataAsync("Boss/Uterus"_s);
		SetAnimation(AnimState::Idle);

		_renderer.SetDrawEnabled(false);

		async_return true;
	}

	bool Uterus::OnActivatedBoss()
	{
		SetHealthByDifficulty(100);
		_renderer.SetDrawEnabled(true);

		MoveInstantly(_originPos, MoveType::Absolute | MoveType::Force);

		_state = StateActive;
		_phase = 0.0f;
		_angle = 0.0f;
		_crabTicks = 0.0f;
		_hasShield = true;
		_touchable = false;
		SetAnimation((AnimState)1073741825);

		_shieldCount = GetDifficultyIndex() * 2 + 4;
		for (std::int32_t i = 0; i < _shieldCount; i++) {
			_shields[i] = std::make_shared<ShieldPart>();
			_shields[i]->Phase = (fTwoPi * i / _shieldCount);
			_shields[i]->OnActivated(ActorActivationDetails(
				_levelHandler,
				Vector3i(std::int32_t(_pos.X), std::int32_t(_pos.Y), _renderer.GetLayer() + 2)
			));
			_levelHandler->AddActor(_shields[i]);
		}
		return true;
	}

	bool Uterus::OnHandleCollision(ActorBase* other)
	{
		// A shot at the shielded Uterus does no harm, but makes it drop a Crab
		if (_hasShield && _state == StateActive && _crabTicks <= 0.0f && runtime_cast<Weapons::ShotBase>(other) != nullptr) {
			_crabTicks = CrabTicks;
		}

		return BossBase::OnHandleCollision(other);
	}

	void Uterus::OnUpdate(float timeMult)
	{
		OnUpdateHitbox();
		HandleBlinking(timeMult);

		if (_state == StateWaiting) {
			return;
		}

		float ticks = timeMult * TicksPerFrame;

		// The cycle of opening and closing
		float oldPhase = _phase;
		_phase = std::fmod(_phase + ticks, CycleTicks);
		if (oldPhase < OpenStartTick && _phase >= OpenStartTick) {
			PlaySfx("Opening"_s);
			SetAnimation(AnimState::Idle);
			SetTransition((AnimState)1073741826, false);
		} else if (oldPhase < CloseStartTick && _phase >= CloseStartTick) {
			PlaySfx("Closing"_s);
			SetAnimation((AnimState)1073741825);
			SetTransition((AnimState)1073741824, false);
		}

		// The Crab drops 70 ticks after the shot
		if (_crabTicks > 0.0f) {
			float oldTicks = _crabTicks;
			_crabTicks -= ticks;
			if (oldTicks > CrabTicks - CrabDropTick && _crabTicks <= CrabTicks - CrabDropTick) {
				SpawnCrab();
			}
		}

		FollowNearestPlayer(timeMult);

		float orbitSpeed = (_hasShield ? ShieldedOrbitSpeed : ExposedOrbitSpeed);
		_angle += orbitSpeed * (fTwoPi / 1024.0f) * ticks;

		float sinAngle, cosAngle;
		sincosApprox(_angle, sinAngle, cosAngle);
		_renderer.SetRotation(fPiOver2 + sinAngle * 0.2f);

		Vector2f pos = _lastPos + Vector2f(sinAngle * WobbleX, cosAngle * WobbleY);
		MoveInstantly(pos, MoveType::Absolute | MoveType::Force);

		if (_hasShield) {
			std::int32_t shieldCount = 0;
			for (std::int32_t i = 0; i < _shieldCount; i++) {
				if (_shields[i] == nullptr) {
					continue;
				}
				if (_shields[i]->GetHealth() <= 0) {
					_shields[i] = nullptr;
					continue;
				}

				shieldCount++;

				float sinShield, cosShield;
				sincosApprox(_angle + _shields[i]->Phase, sinShield, cosShield);
				Vector2f slot = pos + Vector2f(sinShield * ShieldRadius, cosShield * ShieldRadius);

				if (_shields[i]->FallTime > 0.0f) {
					continue;
				}

				if (_shields[i]->ReturnTime > 0.0f) {
					// Flies back to where its place in the ring is by then
					float remaining = _shields[i]->ReturnTime;
					_shields[i]->ReturnTime -= timeMult;
					float factor = std::min<float>(1.0f, timeMult / std::max<float>(remaining, 0.001f));
					Vector2f from = _shields[i]->GetPos();
					_shields[i]->MoveInstantly(from + (slot - from) * factor, MoveType::Absolute | MoveType::Force);
				} else {
					_shields[i]->MoveInstantly(slot, MoveType::Absolute | MoveType::Force);
				}
				_shields[i]->Recover(_angle + _shields[i]->Phase);
			}

			if (shieldCount == 0) {
				_hasShield = false;
			}
		}

		UpdateVulnerability();
		OnUpdateHitbox();
	}

	void Uterus::OnUpdateHitbox()
	{
		UpdateHitbox(38, 60);
	}

	bool Uterus::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() + 2), Explosion::Type::SmokePoof);
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() - 2), Explosion::Type::RF);

		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		StringView text = _levelHandler->GetLevelText(_endText);
		_levelHandler->ShowLevelText(text);

		return BossBase::OnPerish(collider);
	}

	void Uterus::UpdateVulnerability()
	{
		_touchable = (!_hasShield && _phase >= VulnerableStartTick && _phase < VulnerableEndTick);
		SetState(ActorState::IsInvulnerable, !_touchable);
		_canHurtPlayer = _touchable;
	}

	void Uterus::SpawnCrab()
	{
		std::shared_ptr<Enemies::Crab> crab = std::make_shared<Enemies::Crab>();
		crab->OnActivated(ActorActivationDetails(
			_levelHandler,
			Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y + 32, _renderer.GetLayer() - 4)
		));
		_levelHandler->AddActor(crab);
	}

	void Uterus::FollowNearestPlayer(float timeMult)
	{
		Player* player = FindNearestPlayer(FollowDistanceSqr);
		if (player == nullptr) {
			return;
		}

		// Each axis is limited on its own, it is much slower while shielded
		float orbitSpeed = (_hasShield ? ShieldedOrbitSpeed : ExposedOrbitSpeed);
		float limit = orbitSpeed * 0.125f * TicksPerFrame * timeMult;

		Vector2f target = player->GetPos();
		if (_hasShield) {
			target.Y -= HoverHeight;
		}

		_lastPos.X += std::clamp<float>(target.X - _lastPos.X, -limit, limit);
		_lastPos.Y += std::clamp<float>(target.Y - _lastPos.Y, -limit, limit);
	}

	Task<bool> Uterus::ShieldPart::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::ApplyGravitation, false);
		CanCollideWithShots = false;

		// Measured in JJ2+: half the speed is kept on a bounce and the fall is capped at 6 px a tick
		_elasticity = 0.5f;
		_verticalSpeedLimit = 6.0f * TicksPerFrame;

		_health = 3;
		FallTime = 0.0f;
		ReturnTime = 0.0f;

		async_await RequestMetadataAsync("Boss/Uterus"_s);
		SetAnimation((AnimState)1073741827);

		async_return true;
	}

	void Uterus::ShieldPart::OnUpdate(float timeMult)
	{
		UpdateFrozenState(timeMult);

		if (FallTime > 0.0f) {
			EnemyBase::OnUpdate(timeMult);
			FallTime -= timeMult;
			if (FallTime <= 0.0f) {
				ReturnTime = ShieldReturnTicks / TicksPerFrame;
				_speed.X = 0.0f;
				_speed.Y = 0.0f;
			}
		} else {
			HandleBlinking(timeMult);
		}
	}

	void Uterus::ShieldPart::OnUpdateHitbox()
	{
		UpdateHitbox(6, 6);
	}

	bool Uterus::ShieldPart::OnHandleCollision(ActorBase* other)
	{
		if (auto* shotBase = runtime_cast<Weapons::ShotBase>(other)) {
			// The first hit only knocks it off, it takes damage once it has fallen
			if (FallTime > 0.0f) {
				DecreaseHealth(shotBase->GetStrength(), shotBase);
			}

			FallTime = ShieldFallTicks / TicksPerFrame;
			ReturnTime = 0.0f;

			SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::ApplyGravitation, true);

			if (GetState(ActorState::CanBeFrozen)) {
				HandleFrozenStateChange(shotBase);
			}
			return true;
		}

		return EnemyBase::OnHandleCollision(other);
	}

	bool Uterus::ShieldPart::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() + 2), Explosion::Type::Tiny);

		return EnemyBase::OnPerish(collider);
	}

	float Uterus::ShieldPart::GetGravityModifier(float baseGravity, bool isRising) const
	{
		return ReplaceLevelGravity(FallGravity, baseGravity);
	}

	void Uterus::ShieldPart::Recover(float phase)
	{
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::ApplyGravitation, false);

		_renderer.SetRotation(phase);
	}
}
