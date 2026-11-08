#include "Bat.h"
#include "../../ILevelHandler.h"

#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	namespace
	{
		constexpr AnimState AnimTakeoff = (AnimState)1073741824;
		constexpr AnimState AnimTakeoffEnd = (AnimState)1073741825;
	}

	Bat::Bat()
		: _speedPerTickY(0.0f), _tickTime(0.0f), _noiseCooldown(0.0f), _counter(0), _frame(0), _batState(BatState::Hanging)
	{
	}

	void Bat::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Bat"_s);
	}

	Task<bool> Bat::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_originPos = _pos;

		SetState(ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		SetHealthByDifficulty(1);
		_scoreValue = 100;

		async_await RequestMetadataAsync("Enemy/Bat"_s);
		SetAnimation(AnimState::Idle);
		SetAnimationFrame(0);

		async_return true;
	}

	void Bat::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		_tickTime += timeMult * TicksPerFrame;
		while (_tickTime >= 1.0f) {
			_tickTime -= 1.0f;
			AdvanceTick();
		}

		if (_batState == BatState::Flying) {
			if (_noiseCooldown > 0.0f) {
				_noiseCooldown -= timeMult;
			} else {
				_noiseCooldown = 60.0f;
				PlaySfx("Noise"_s);
			}
		}
	}

	void Bat::AdvanceTick()
	{
		switch (_batState) {
			case BatState::Hanging: {
				Vector2f targetPos;
				if (FindTarget(targetPos, WakeDistance, _pos)) {
					_batState = BatState::Waking;
					_counter = 0;
					SetAnimation(AnimTakeoffEnd);
					SetAnimationFrame(0);
					break;
				}

				_counter++;
				if (_counter >= TwitchCycle) {
					_counter = 0;
				}
				// The original counts past its single hanging frame into the take-off, so it twitches in the last ticks of the cycle
				std::int32_t frame = _counter / TicksPerAnimFrame - (TwitchCycle / TicksPerAnimFrame - TwitchFrames - 1);
				if (frame > 0) {
					if (_currentAnimation == nullptr || _currentAnimation->State != AnimTakeoff) {
						SetAnimation(AnimTakeoff);
					}
					SetAnimationFrame(frame - 1);
				} else {
					if (_currentAnimation == nullptr || _currentAnimation->State != AnimState::Idle) {
						SetAnimation(AnimState::Idle);
					}
					SetAnimationFrame(0);
				}
				break;
			}

			case BatState::Waking: {
				_counter++;
				if (_counter >= WakeTicks) {
					_batState = BatState::Flying;
					_counter = 0;
					_frame = 0;
					_speedPerTickY = 0.0f;
					_noiseCooldown = 0.0f;
					SetAnimation(AnimState::Walk);
					SetAnimationFrame(0);
				} else {
					SetAnimationFrame(1);
				}
				break;
			}

			case BatState::Flying: {
				Vector2f targetPos;
				if (!FindTarget(targetPos, ChaseDistance, _originPos)) {
					targetPos = _originPos;
					if ((_pos - _originPos).Length() < RoostDistance) {
						_batState = BatState::Hanging;
						_counter = -7;
						MoveInstantly(Vector2f(_pos.X, _originPos.Y), MoveType::Absolute | MoveType::Force);
						SetAnimation(AnimState::Idle);
						SetAnimationFrame(0);
						break;
					}
				}

				// Can't fly into the water
				float waterLevel = _levelHandler->GetWaterLevel();
				if (targetPos.Y > waterLevel - 20.0f) {
					targetPos.Y = waterLevel - 20.0f;
				}

				float stepX = Random().FastFloat(0.0f, MaxStepX);
				if (targetPos.X < _pos.X) {
					stepX = -stepX;
				}
				MoveInstantly(Vector2f(stepX, _speedPerTickY), MoveType::Relative | MoveType::Force);
				SetFacingLeft(targetPos.X < _pos.X);

				// A new speed up or down each wing beat
				_counter++;
				if (_counter >= TicksPerAnimFrame) {
					_counter = 0;
					_frame = (_frame + 1) % std::max(_renderer.FrameCount, 1);
					float speedY = Random().FastFloat(0.0f, MaxSpeedY);
					_speedPerTickY = (_pos.Y > targetPos.Y ? -speedY : speedY);
				}
				SetAnimationFrame(_frame);
				break;
			}
		}
	}

	void Bat::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteValue<std::uint8_t>((std::uint8_t)_batState);
		dest.WriteValueAsLE<float>((float)_counter);
	}

	void Bat::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		std::uint8_t state = src.ReadValue<std::uint8_t>();
		_batState = (state <= (std::uint8_t)BatState::Flying ? (BatState)state : BatState::Hanging);
		_counter = (std::int32_t)src.ReadValueAsLE<float>();
		_frame = 0;
		_speedPerTickY = 0.0f;
		switch (_batState) {
			case BatState::Hanging: SetAnimation(AnimState::Idle); break;
			case BatState::Waking: SetAnimation(AnimTakeoffEnd); break;
			case BatState::Flying: SetAnimation(AnimState::Walk); break;
		}
		SetAnimationFrame(_batState == BatState::Waking ? 1 : 0);
	}

	void Bat::OnUpdateHitbox()
	{
		UpdateHitbox(24, 24);
	}

	bool Bat::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	bool Bat::FindTarget(Vector2f& targetPos, float maxDistance, const Vector2f& from) const
	{
		float nearest = maxDistance;
		bool found = false;
		for (auto& player : _levelHandler->GetPlayers()) {
			if (player->GetHealth() <= 0) {
				continue;
			}
			float distance = (player->GetPos() - from).Length();
			if (distance < nearest) {
				nearest = distance;
				targetPos = player->GetPos();
				found = true;
			}
		}
		return found;
	}
}
