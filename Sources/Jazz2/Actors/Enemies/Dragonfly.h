#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Dragonfly
		
		Flying insect that bobs in place until a player comes near, then buzzes in a straight line to
		a point beside and above the player and hovers there, following the player around. Stays above
		the water surface. Defeated by a single hit.
	*/
	class Dragonfly : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Dragonfly();
		~Dragonfly();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnHitWall(float timeMult) override;
		void OnHitFloor(float timeMult) override;
		void OnHitCeiling(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		static constexpr std::int32_t StateIdle = 0;
		static constexpr std::int32_t StateFlying = 1;
		static constexpr std::int32_t StateHovering = 2;
		static constexpr std::int32_t StateDiving = 3;
		static constexpr std::int32_t StateReturning = 4;

		std::int32_t _state;
		std::int32_t _counter;
		std::int32_t _animTicks;
		float _tickTime;
		Vector2f _originPos;
		Vector2f _basePos;
		Vector2f _targetPos;
		Vector2f _diveFrom;
		std::shared_ptr<AudioBufferPlayer> _noise;

		void AdvanceTick();
		Player* FindNearestPlayer(float maxDistance, bool aboveWaterOnly) const;
		bool FindTargetPos(Vector2f& targetPos, float maxDistance) const;
		void StopNoise();
	};
}