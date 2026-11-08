#include "Rapier.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Enemies
{
	namespace
	{
		constexpr AnimState AnimLunge = (AnimState)1073741824;
		constexpr AnimState AnimWindUp = (AnimState)1073741825;
		constexpr AnimState AnimBrake = (AnimState)1073741826;

		constexpr float AttackDistance = 180.0f;
		constexpr float FloatRadius = 16.0f;
		constexpr std::int32_t FloatPeriod = 1024;
		constexpr std::int32_t FloatTicksBeforeAttack = 370;
		constexpr std::int32_t WindUpTicks = 32;
		constexpr std::int32_t LungeTicks = 71;
		constexpr std::int32_t BrakeTicks = 20;
		constexpr std::int32_t ReturnTicks = 64;
		constexpr float LungeAccelerationX = 1.0f / 16.0f;
		constexpr float LungeSpringY = 1.0f / 256.0f;
		constexpr float BrakeFactor = 0.875f;
	}

	Rapier::Rapier()
		: _rapierState(RapierState::Floating), _counter(0), _floatCounter(FloatPeriod / 4), _tickTime(0.0f), _noiseCooldown(Random().FastFloat(200.0f, 400.0f))
	{
	}

	void Rapier::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Rapier"_s);
	}

	Task<bool> Rapier::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::ApplyGravitation, false);

		SetHealthByDifficulty(2);
		_scoreValue = 300;

		_originPos = _returnFrom = _pos;

		async_await RequestMetadataAsync("Enemy/Rapier"_s);
		SetFacingLeft(Random().NextBool());
		ShowFrame(AnimState::Idle, 0);
		MoveInstantly(GetFloatPos(), MoveType::Absolute | MoveType::Force);

		async_return true;
	}

	void Rapier::OnUpdate(float timeMult)
	{
		OnUpdateHitbox();
		HandleBlinking(timeMult);
		UpdateFrozenState(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		_tickTime += timeMult * TicksPerFrame;
		while (_tickTime >= 1.0f) {
			_tickTime -= 1.0f;
			AdvanceTick();
		}

		if (_noiseCooldown > 0.0f) {
			_noiseCooldown -= timeMult;
		} else {
			_noiseCooldown = Random().FastFloat(300.0f, 600.0f);
			if (Random().NextFloat() < 0.5f) {
				PlaySfx("Noise"_s, 0.7f);
			}
		}
	}

	void Rapier::AdvanceTick()
	{
		_counter++;

		switch (_rapierState) {
			case RapierState::Floating: {
				_floatCounter++;
				Vector2f targetPos;
				bool found = (_floatCounter >= FloatTicksBeforeAttack && FindTarget(targetPos));
				MoveInstantly(GetFloatPos(), MoveType::Absolute | MoveType::Force);
				ShowFrame(AnimState::Idle, _floatCounter / 8);

				// Measured in JJ2+: it turns to the player only when it winds up
				if (found && (targetPos - _pos).SqrLength() < AttackDistance * AttackDistance) {
					SetFacingLeft(targetPos.X < _pos.X);
					ChangeState(RapierState::WindingUp);
				}
				break;
			}

			case RapierState::WindingUp: {
				if (_counter >= WindUpTicks) {
					_speedPerTick = Vector2f::Zero;
					ChangeState(RapierState::Lunging);
					PlaySfx("Attack"_s, 0.7f);
					ShowFrame(AnimLunge, 0);
				} else {
					ShowFrame(AnimWindUp, _counter / 4);
				}
				break;
			}

			case RapierState::Lunging: {
				Vector2f targetPos;
				if (!FindTarget(targetPos)) {
					targetPos = _pos;
				}
				_speedPerTick.X += (IsFacingLeft() ? -LungeAccelerationX : LungeAccelerationX);
				_speedPerTick.Y += (targetPos.Y - _pos.Y) * LungeSpringY;
				MoveInstantly(_speedPerTick, MoveType::Relative | MoveType::Force);
				ShowFrame(AnimLunge, _counter / 8);

				if (_counter >= LungeTicks) {
					ChangeState(RapierState::Braking);
				}
				break;
			}

			case RapierState::Braking: {
				_speedPerTick *= BrakeFactor;
				MoveInstantly(_speedPerTick, MoveType::Relative | MoveType::Force);
				ShowFrame(AnimBrake, _counter / 4);

				if (_counter >= BrakeTicks) {
					_returnFrom = _pos;
					SetFacingLeft(_originPos.X < _pos.X);
					ChangeState(RapierState::Returning);
				}
				break;
			}

			case RapierState::Returning: {
				float t = std::min((float)_counter / ReturnTicks, 1.0f);
				Vector2f floatStart = _originPos + Vector2f(0.0f, FloatRadius);
				MoveInstantly(_returnFrom + (floatStart - _returnFrom) * t, MoveType::Absolute | MoveType::Force);
				ShowFrame(AnimState::Idle, _counter / 8);

				if (_counter >= ReturnTicks) {
					_floatCounter = 0;
					ChangeState(RapierState::Floating);
				}
				break;
			}
		}
	}

	void Rapier::ChangeState(RapierState state)
	{
		_rapierState = state;
		_counter = 0;
	}

	void Rapier::ShowFrame(AnimState anim, std::int32_t frame)
	{
		SetAnimation(anim);
		if (_renderer.FrameCount > 0) {
			SetAnimationFrame(frame % _renderer.FrameCount);
		}
	}

	Vector2f Rapier::GetFloatPos() const
	{
		float angle = (float)(_floatCounter % FloatPeriod) * (fTwoPi / FloatPeriod);
		return _originPos + Vector2f(sinf(angle) * FloatRadius, cosf(angle * 2.0f) * FloatRadius);
	}

	void Rapier::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteValue<std::uint8_t>((std::uint8_t)_rapierState);
		dest.WriteVariableInt32(_counter);
		dest.WriteVariableInt32(_floatCounter);
		dest.WriteValueAsLE<float>(_speedPerTick.X);
		dest.WriteValueAsLE<float>(_speedPerTick.Y);
		dest.WriteValueAsLE<float>(_returnFrom.X);
		dest.WriteValueAsLE<float>(_returnFrom.Y);
	}

	void Rapier::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		std::uint8_t state = src.ReadValue<std::uint8_t>();
		_rapierState = (state <= (std::uint8_t)RapierState::Returning ? (RapierState)state : RapierState::Floating);
		_counter = src.ReadVariableInt32();
		_floatCounter = src.ReadVariableInt32();
		_speedPerTick.X = src.ReadValueAsLE<float>();
		_speedPerTick.Y = src.ReadValueAsLE<float>();
		_returnFrom.X = src.ReadValueAsLE<float>();
		_returnFrom.Y = src.ReadValueAsLE<float>();
	}

	bool Rapier::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(ParticleDebrisEffect::Dissolve, Vector2f::Zero);
		PlaySfx("Die"_s);
		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	bool Rapier::FindTarget(Vector2f& targetPos)
	{
		float nearestSqr = FLT_MAX;
		bool found = false;
		for (auto* player : _levelHandler->GetPlayers()) {
			if (player->GetHealth() <= 0) {
				continue;
			}
			float distanceSqr = (player->GetPos() - _pos).SqrLength();
			if (distanceSqr < nearestSqr) {
				nearestSqr = distanceSqr;
				targetPos = player->GetPos();
				found = true;
			}
		}
		return found;
	}
}
