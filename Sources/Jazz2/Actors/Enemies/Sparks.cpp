#include "Sparks.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	namespace
	{
		constexpr float NoticeDistance = 256.0f;
		constexpr float ReachDistance = 16.0f;
		constexpr float Acceleration = 1.0f / 16.0f;
		constexpr float CoastFactor = 31.0f / 32.0f;
		constexpr float FrameTicks = 6.0f;
	}

	Sparks::Sparks()
		: _tickTime(0.0f)
	{
	}

	void Sparks::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Sparks"_s);
	}

	Task<bool> Sparks::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::ApplyGravitation, false);

		SetHealthByDifficulty(1);
		_scoreValue = 100;

		async_await RequestMetadataAsync("Enemy/Sparks"_s);
		SetFacingLeft(true);
		SetAnimation(AnimState::Idle);
		if (_renderer.FrameCount > 0) {
			SetAnimationTicks(_renderer.FrameCount * FrameTicks);
		}

		async_return true;
	}

	void Sparks::OnUpdate(float timeMult)
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
	}

	void Sparks::AdvanceTick()
	{
		// Chases an invulnerable player too, as in JJ2+
		Player* target = FindNearestPlayer(NoticeDistance * NoticeDistance);
		if (target == nullptr) {
			// Nobody around, it stops where it is
			_speedPerTick *= CoastFactor;
			return;
		}

		// Like the original it only chases a player who turns away, one facing it (or right next to it) makes it slow down
		Vector2f diff = target->GetPos() - _pos;
		bool watched = (diff.X > 0.0f ? target->IsFacingLeft() : !target->IsFacingLeft());
		if (watched || (std::abs(diff.X) <= ReachDistance && std::abs(diff.Y) <= ReachDistance)) {
			_speedPerTick *= CoastFactor;
		} else {
			// 2, 3 or 4 px per tick by difficulty
			float maxSpeed = 2.0f + (float)GetDifficultyIndex();
			Vector2f targetSpeed = Vector2f(std::clamp(diff.X, -maxSpeed, maxSpeed), std::clamp(diff.Y, -maxSpeed, maxSpeed));
			_speedPerTick.X = ApproachSpeed(_speedPerTick.X, targetSpeed.X);
			_speedPerTick.Y = ApproachSpeed(_speedPerTick.Y, targetSpeed.Y);
		}
		SetFacingLeft(diff.X > 0.0f);

		MoveInstantly(_speedPerTick, MoveType::Relative | MoveType::Force);
	}

	float Sparks::ApproachSpeed(float speed, float target)
	{
		if (speed < target) {
			return std::min(speed + Acceleration, target);
		}
		return std::max(speed - Acceleration, target);
	}

	void Sparks::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteValueAsLE<float>(_speedPerTick.X);
		dest.WriteValueAsLE<float>(_speedPerTick.Y);
		dest.WriteValue<std::uint8_t>(0);	// Unused
	}

	void Sparks::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		_speedPerTick.X = src.ReadValueAsLE<float>();
		_speedPerTick.Y = src.ReadValueAsLE<float>();
		src.ReadValue<std::uint8_t>();	// Unused
	}

	bool Sparks::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}