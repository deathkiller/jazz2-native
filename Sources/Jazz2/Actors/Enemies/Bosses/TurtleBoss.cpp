#include "TurtleBoss.h"
#include "../../../ILevelHandler.h"
#include "../../Player.h"
#include "../../Explosion.h"
#include "../TurtleShell.h"

#include "../../../../nCine/Base/Random.h"

#include <cmath>

namespace Jazz2::Actors::Bosses
{
	TurtleBoss::TurtleBoss()
		: _state(StateWaiting), _endText(0), _walkTicks(0.0f), _turnAcc(0.0f), _attackAcc(0.0f), _attackTicks(0.0f),
			_attackEndStart(0.0f), _maceThrown(false), _aimCaptured(false)
	{
	}

	TurtleBoss::~TurtleBoss()
	{
		if (_mace != nullptr) {
			_mace->DecreaseHealth(INT32_MAX);
			_mace = nullptr;
		}
	}

	void TurtleBoss::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Boss/TurtleBoss"_s);
		PreloadMetadataAsync("Boss/TurtleBossShell"_s);
	}

	Task<bool> TurtleBoss::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_endText = details.Params[1];
		_scoreValue = 5000;

		async_await RequestMetadataAsync("Boss/TurtleBoss"_s);
		SetAnimation(AnimState::Idle);

		SetFacingLeft(true);

		async_return true;
	}

	bool TurtleBoss::OnActivatedBoss()
	{
		SetHealthByDifficulty(100);
		StartWalking();
		return true;
	}

	void TurtleBoss::OnUpdate(float timeMult)
	{
		BossBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		switch (_state) {
			case StateWalking: UpdateWalking(timeMult); break;
			case StateAttacking: UpdateAttacking(timeMult); break;
		}
	}

	bool TurtleBoss::OnPerish(ActorBase* collider)
	{
		float shellSpeedY;
		if (_pos.Y > _levelHandler->GetWaterLevel()) {
			shellSpeedY = -0.65f;
		} else if (_levelHandler->IsReforged()) {
			shellSpeedY = -1.1f;
		} else {
			shellSpeedY = -0.98f;
		}

		std::shared_ptr<Enemies::TurtleShell> shell = std::make_shared<Enemies::TurtleShell>();
		uint8_t shellParams[9];
		EventParamsWriter writer(shellParams);
		writer.SetFloat(0, _speed.X * 1.1f);
		writer.SetFloat(4, shellSpeedY);
		writer.SetUint8(8, 2);
		shell->OnActivated(ActorActivationDetails(
			_levelHandler,
			Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer()),
			shellParams
		));
		_levelHandler->AddActor(shell);

		Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() - 2), Explosion::Type::SmokeGray);

		CreateParticleDebrisOnPerish(ParticleDebrisEffect::Standard, Vector2f::Zero);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		StringView text = _levelHandler->GetLevelText(_endText);
		_levelHandler->ShowLevelText(text);

		return BossBase::OnPerish(collider);
	}

	void TurtleBoss::StartWalking()
	{
		_state = StateWalking;
		_walkTicks = 0.0f;
		_turnAcc = 0.0f;
		_attackAcc = 0.0f;
		_speed.X = (IsFacingLeft() ? -WalkSpeed : WalkSpeed);
		SetAnimation(AnimState::Walk);
	}

	void TurtleBoss::UpdateWalking(float timeMult)
	{
		float ticks = timeMult * TicksPerFrame;

		if (!CanMoveToPosition(_speed.X, 0)) {
			SetFacingLeft(!IsFacingLeft());
			_speed.X = -_speed.X;
		}

		_walkTicks += ticks;
		_turnAcc += ticks;
		_attackAcc += ticks;

		if (_turnAcc >= TurnPeriodTicks) {
			_turnAcc = std::fmod(_turnAcc, TurnPeriodTicks);

			Player* player = FindNearestPlayer(NoticeDistanceSqr);
			if (player != nullptr) {
				bool faceLeft = (player->GetPos().X < _pos.X);
				if (faceLeft != IsFacingLeft()) {
					SetFacingLeft(faceLeft);
					_speed.X = (faceLeft ? -WalkSpeed : WalkSpeed);
					PlaySfx("AttackStart"_s, 0.4f);
				}
			}
		}

		if (_attackAcc >= AttackPeriodTicks) {
			_attackAcc = std::fmod(_attackAcc, AttackPeriodTicks);

			if (_walkTicks > AttackMinTicks) {
				Player* player = FindNearestPlayer(NoticeDistanceSqr);
				if (player != nullptr) {
					StartAttack(player);
				}
			}
		}
	}

	void TurtleBoss::StartAttack(Player* target)
	{
		_state = StateAttacking;
		_speed.X = 0.0f;
		_attackTicks = 0.0f;
		_maceThrown = false;
		_aimCaptured = false;
		_target = target->shared_from_this();

		std::int32_t difficulty = GetDifficultyIndex();
		// The mace stays out for as long as the boss stands, both scale with the difficulty
		_attackEndStart = ThrowTicks + (AttackAnimTicks - ThrowTicks) + std::ceil((128.0f - (AttackAnimTicks - ThrowTicks)) / (float)(difficulty + 1));

		PlaySfx("AttackStart"_s);
		SetTransition((AnimState)1073741824, false, [this]() {
			if (_state == StateAttacking) {
				SetTransition((AnimState)1073741825, false);
			}
		});
	}

	void TurtleBoss::UpdateAttacking(float timeMult)
	{
		float ticks = timeMult * TicksPerFrame;
		_attackTicks += ticks;

		std::int32_t difficulty = GetDifficultyIndex();

		// The aim point is taken from where the player stands a few animation frames before the throw
		if (!_aimCaptured && _attackTicks >= (float)(4 + 4 * difficulty) * AttackFrameTicks) {
			_aimCaptured = true;
			if (auto target = _target.lock()) {
				_aimPos = target->GetPos();
				SetFacingLeft(_aimPos.X < _pos.X);
			} else {
				_aimPos = _pos + Vector2f(IsFacingLeft() ? -100.0f : 100.0f, 0.0f);
			}
		}

		if (!_maceThrown && _attackTicks >= ThrowTicks) {
			_maceThrown = true;
			if (!_aimCaptured) {
				_aimPos = _pos;
			}

			Vector2f spawnPos = _pos;
			_mace = std::make_shared<Mace>();
			_mace->Setup(shared_from_this(), _aimPos - spawnPos, difficulty);
			_mace->OnActivated(ActorActivationDetails(
				_levelHandler,
				Vector3i((std::int32_t)spawnPos.X, (std::int32_t)spawnPos.Y, _renderer.GetLayer() + 2)
			));
			_levelHandler->AddActor(_mace);
		}

		if (_attackTicks >= _attackEndStart + AttackEndTicks) {
			_mace = nullptr;
			StartWalking();
		} else if (_attackTicks >= _attackEndStart && _attackTicks - ticks < _attackEndStart) {
			PlaySfx("AttackEnd"_s);
			SetTransition((AnimState)1073741826, false, [this]() {
				if (_state == StateAttacking) {
					SetAnimation(AnimState::Idle);
				}
			});
			SetAnimationTicks(AttackEndTicks);
		}
	}

	TurtleBoss::Mace::~Mace()
	{
#if defined(WITH_AUDIO)
		if (_sound != nullptr) {
			_sound->Stop();
			_sound = nullptr;
		}
#endif
	}

	void TurtleBoss::Mace::Setup(const std::shared_ptr<ActorBase>& owner, Vector2f aim, std::int32_t difficulty)
	{
		_owner = owner;
		_aim = aim;
		_clock = 0.0f;
		_clockStep = (float)(difficulty + 1);
	}

	Task<bool> TurtleBoss::Mace::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);
		CanCollideWithShots = false;

		_health = INT32_MAX;

		async_await RequestMetadataAsync("Boss/TurtleBoss"_s);
		SetAnimation((AnimState)1073741827);

		_originPos = _pos;

#if defined(WITH_AUDIO)
		_sound = PlaySfx("Mace"_s, 0.7f);
		if (_sound != nullptr) {
			_sound->SetLooping(true);
		}
#endif

		async_return true;
	}

	void TurtleBoss::Mace::OnUpdate(float timeMult)
	{
		float ticks = timeMult * TicksPerFrame;
		_clock += _clockStep * ticks;

		if (_clock < OutAndBackClock) {
			// Out to the aim point and back along a sine, the clock covers a half turn
			float factor = std::sin(_clock * fPi / OutAndBackClock);
			MoveInstantly(_originPos + _aim * factor, MoveType::Absolute | MoveType::Force);
		} else {
			auto owner = _owner.lock();
			if (owner == nullptr || _clock > EndClock) {
				DecreaseHealth(INT32_MAX);
				return;
			}

			Vector2f target = owner->GetPos() + Vector2f(owner->IsFacingLeft() ? -MaceReturnOffset : MaceReturnOffset, 0.0f);
			float keep = std::pow(0.5f, ticks);
			MoveInstantly(target + (_pos - target) * keep, MoveType::Absolute | MoveType::Force);
		}

#if defined(WITH_AUDIO)
		if (_sound != nullptr) {
			_sound->SetPosition(Vector3f(_pos.X, _pos.Y, 0.8f));
		}
#endif
	}

	void TurtleBoss::Mace::OnUpdateHitbox()
	{
		UpdateHitbox(18, 18);
	}
}
