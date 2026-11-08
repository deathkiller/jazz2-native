#include "Sparks.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	namespace
	{
		constexpr float NoticeDistance = 180.0f;
		constexpr float ReachDistance = 16.0f;
		constexpr float Acceleration = 1.0f / 16.0f;
		constexpr float MaxSpeed = 3.0f;
		constexpr float CoastFactor = 31.0f / 32.0f;
	}

	Sparks::Sparks()
		: _tickTime(0.0f), _coasting(false)
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
		// Measured in JJ2+: once it reaches the player, it only coasts and never chases again
		if (!_coasting) {
			Player* target = nullptr;
			float targetDistanceSqr = NoticeDistance * NoticeDistance;
			float waterLevel = _levelHandler->GetWaterLevel();
			for (auto* player : _levelHandler->GetPlayers()) {
				// Chases an invulnerable player too, as in JJ2+
				if (player->GetHealth() <= 0 || player->GetPos().Y >= waterLevel) {
					continue;
				}
				float distanceSqr = (player->GetPos() - _pos).SqrLength();
				if (distanceSqr < targetDistanceSqr) {
					target = player;
					targetDistanceSqr = distanceSqr;
				}
			}

			if (target != nullptr && targetDistanceSqr < ReachDistance * ReachDistance) {
				_coasting = true;
				SetFacingLeft(!IsFacingLeft());
			} else if (target != nullptr) {
				Vector2f targetPos = target->GetPos();
				_speedPerTick.X = std::clamp(_speedPerTick.X + (targetPos.X > _pos.X ? Acceleration : -Acceleration), -MaxSpeed, MaxSpeed);
				_speedPerTick.Y = std::clamp(_speedPerTick.Y + (targetPos.Y > _pos.Y ? Acceleration : -Acceleration), -MaxSpeed, MaxSpeed);
				SetFacingLeft(_speedPerTick.X >= 0.0f);
			}
		}

		if (_coasting) {
			_speedPerTick *= CoastFactor;
		}

		MoveInstantly(_speedPerTick, MoveType::Relative | MoveType::Force);
	}

	void Sparks::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteValueAsLE<float>(_speedPerTick.X);
		dest.WriteValueAsLE<float>(_speedPerTick.Y);
		dest.WriteValue<std::uint8_t>(_coasting ? 1 : 0);
	}

	void Sparks::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		_speedPerTick.X = src.ReadValueAsLE<float>();
		_speedPerTick.Y = src.ReadValueAsLE<float>();
		_coasting = (src.ReadValue<std::uint8_t>() != 0);
	}

	bool Sparks::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}