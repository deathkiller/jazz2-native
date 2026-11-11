#include "Bilsy.h"
#include "../../../ILevelHandler.h"
#include "../../Player.h"
#include "../../Explosion.h"

#include "../../../../nCine/Base/Random.h"

#include <cmath>

namespace Jazz2::Actors::Bosses
{
	Bilsy::Bilsy()
		: _state(StateDormant), _stateTime(0.0f), _fireballThrown(false), _theme(0), _endText(0)
	{
	}

	void Bilsy::Preload(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		switch (theme) {
			case 0:
			default:
				PreloadMetadataAsync("Boss/Bilsy"_s);
				break;
			case 1: // Xmas
				PreloadMetadataAsync("Boss/BilsyXmas"_s);
				break;
		}
	}

	Task<bool> Bilsy::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_theme = details.Params[0];
		_endText = details.Params[1];
		_originPos = _pos;
		_scoreValue = 3000;

		SetState(ActorState::ApplyGravitation, false);

		switch (_theme) {
			case 0:
			default:
				async_await RequestMetadataAsync("Boss/Bilsy"_s);
				break;
			case 1: // Xmas
				async_await RequestMetadataAsync("Boss/BilsyXmas"_s);
				break;
		}

		SetAnimation(AnimState::Idle);

		async_return true;
	}

	bool Bilsy::OnActivatedBoss()
	{
		SetHealthByDifficulty(120);

		// Measured in JJ2+: it stands on the floor below where the level puts it
		OnUpdateHitbox();
		Tiles::TileCollisionParams probe = { Tiles::TileDestructType::None, true };
		for (std::int32_t i = 0; i < GroundSearchDistance; i++) {
			AABBf feet = AABBInner;
			feet.T = feet.B;
			feet.B += 1.0f;
			if (!_levelHandler->IsPositionEmpty(this, feet, probe)) {
				break;
			}
			MoveInstantly(Vector2f(0.0f, 1.0f), MoveType::Relative | MoveType::Force);
			OnUpdateHitbox();
		}
		_originPos = _pos;
		_state = StateStartWait;
		_stateTime = StartDelay;
		SetFacingLeft(true);
		return true;
	}

	void Bilsy::OnUpdate(float timeMult)
	{
		BossBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f || _state == StateDormant) {
			return;
		}

		switch (_state) {
			case StateStartWait: {
				if (_stateTime <= 0.0f) {
					_state = StateAppear;
					_stateTime = AppearTime;
					SetAnimation((AnimState)1073741824);
		SetAnimationTicks(AppearTicks);
					PlaySfx("Appear"_s, 0.8f);
				}
				break;
			}
			case StateAppear: {
				if (_stateTime <= 0.0f) {
					_state = StateIdle;
					_stateTime = IdleTime;
					SetAnimation(AnimState::Idle);

					// The original turns to the player again here only on Hard
					if (_levelHandler->GetDifficulty() >= GameDifficulty::Hard) {
						if (auto target = _target.lock()) {
							float dx = target->GetPos().X - _pos.X;
							if (dx < -1.0f) {
								SetFacingLeft(true);
							} else if (dx > 1.0f) {
								SetFacingLeft(false);
							}
						}
					}
				}
				break;
			}
			case StateIdle: {
				if (_stateTime <= 0.0f) {
					Player* player = FindTarget();
					if (player != nullptr) {
						_target = player->shared_from_this();

						float dx = player->GetPos().X - _pos.X;
						bool isFacing = (dx != 0.0f && (dx < 0.0f) == IsFacingLeft());
						if (isFacing) {
							_state = StateThrow;
							_stateTime = ThrowTime;
							_fireballThrown = false;
							SetAnimation((AnimState)1073741826);
							SetAnimationTicks(ThrowTime * TicksPerFrame);
						} else {
							_state = StateVanish;
							_stateTime = VanishTime;
							SetAnimation((AnimState)1073741825);
		SetAnimationTicks(VanishTicks);
							PlaySfx("Disappear"_s, 0.8f);
						}
					}
				}
				break;
			}
			case StateThrow: {
				if (!_fireballThrown && ThrowTime - _stateTime >= ThrowFireballTime) {
					_fireballThrown = true;
					PlaySfx("ThrowFireball"_s);

					std::shared_ptr<Fireball> fireball = std::make_shared<Fireball>();
					uint8_t fireballParams[2] = { _theme, (uint8_t)(IsFacingLeft() ? 1 : 0) };
					fireball->OnActivated(ActorActivationDetails(
						_levelHandler,
						Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -26 : 26), (std::int32_t)_pos.Y - 20, _renderer.GetLayer() + 2),
						fireballParams
					));
					fireball->SetTarget(_target.lock());
					_levelHandler->AddActor(fireball);
				}

				if (_stateTime <= 0.0f) {
					_state = StateVanish;
					_stateTime = VanishTime;
					SetAnimation((AnimState)1073741825);
		SetAnimationTicks(VanishTicks);
					PlaySfx("Disappear"_s, 0.8f);
				}
				break;
			}
			case StateVanish: {
				if (_stateTime <= 0.0f) {
					Teleport();
				}
				break;
			}
		}

		_stateTime -= timeMult;
	}

	void Bilsy::OnUpdateHitbox()
	{
		UpdateHitbox(20, 60);
	}

	bool Bilsy::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		StringView text = _levelHandler->GetLevelText(_endText);
		_levelHandler->ShowLevelText(text);

		return BossBase::OnPerish(collider);
	}

	Player* Bilsy::FindTarget() const
	{
		Player* result = nullptr;
		float bestSqr = NoticeDistanceSqr;

		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			if (player->GetHealth() <= 0) {
				continue;
			}

			float distSqr = (player->GetPos() - _pos).SqrLength();
			if (distSqr < bestSqr) {
				bestSqr = distSqr;
				result = player;
			}
		}

		return result;
	}

	void Bilsy::Teleport()
	{
		float steps = (float)((std::int32_t)Random().Next(0, 32) - 15);
		MoveInstantly(Vector2f(_originPos.X + steps * TeleportStep, _originPos.Y), MoveType::Absolute | MoveType::Force);

		if (auto target = _target.lock()) {
			SetFacingLeft(target->GetPos().X < _pos.X);
		}

		_state = StateAppear;
		_stateTime = AppearTime;
		SetAnimation((AnimState)1073741824);
		SetAnimationTicks(AppearTicks);
		PlaySfx("Appear"_s, 0.8f);
	}

	Task<bool> Bilsy::Fireball::OnActivatedAsync(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		SetFacingLeft(details.Params[1] != 0);
		_timeLeft = Lifetime;
		_heading = (IsFacingLeft() ? fPi : 0.0f);
		_speedValue = InitialSpeedValue;

		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);
		CanCollideWithShots = false;

		_health = INT32_MAX;
		_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * (_speedValue / 8.0f) * TicksPerFrame;
		_speed.Y = 0.0f;

		switch (theme) {
			case 0:
			default:
				async_await RequestMetadataAsync("Boss/Bilsy"_s);
				break;

			case 1: // Xmas
				async_await RequestMetadataAsync("Boss/BilsyXmas"_s);
				break;
		}

		SetAnimation((AnimState)1073741828);

		PlaySfx("FireStart"_s);

		async_return true;
	}

	void Bilsy::Fireball::OnUpdate(float timeMult)
	{
		FollowTarget(timeMult);

		MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);

		if (_timeLeft <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		} else {
			_timeLeft -= timeMult;
		}

		if (_speed.X < 0.0f) {
			SetFacingLeft(true);
			_renderer.SetRotation(atan2Approx(-_speed.Y, -_speed.X));
		} else {
			SetFacingLeft(false);
			_renderer.SetRotation(atan2Approx(_speed.Y, _speed.X));
		}
	}

	void Bilsy::Fireball::OnUpdateHitbox()
	{
		UpdateHitbox(18, 18);
	}

	void Bilsy::Fireball::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = 0.85f;
		light.Brightness = 0.4f;
		light.RadiusNear = 0.0f;
		light.RadiusFar = 30.0f;
	}

	bool Bilsy::Fireball::OnHandleCollision(ActorBase* other)
	{
		if (auto* player = runtime_cast<Player>(other)) {
			DecreaseHealth(INT32_MAX);
		}

		return ActorBase::OnHandleCollision(other);
	}

	bool Bilsy::Fireball::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.GetLayer() + 2), Explosion::Type::RF);

		return EnemyBase::OnPerish(collider);
	}

	void Bilsy::Fireball::FollowTarget(float timeMult)
	{
		auto target = _target.lock();
		if (target == nullptr) {
			return;
		}

		Vector2f diff = target->GetPos() - _pos;
		float dist = diff.Length();
		if (dist < 0.001f) {
			return;
		}

		// The heading always points at the target, only the speed depends on how much the bearing changed
		float angle = atan2Approx(diff.Y, diff.X);
		float delta = std::abs(angle - _heading);
		if (delta > fPi) {
			delta = fTwoPi - delta;
		}
		_heading = angle;

		float ticks = std::max<float>(timeMult * TicksPerFrame, 0.01f);
		float unitsPerTick = std::min<float>(delta / fTwoPi * 1024.0f / ticks, 512.0f);
		float wanted = (1024.0f - unitsPerTick) / 32.0f;
		_speedValue += (wanted - _speedValue) * (1.0f - std::pow(0.5f, ticks));
		_speedValue = std::max<float>(_speedValue, MinSpeedValue);

		float speed = _speedValue / 8.0f * TicksPerFrame;
		_speed.X = diff.X / dist * speed;
		_speed.Y = diff.Y / dist * speed;
	}
}
