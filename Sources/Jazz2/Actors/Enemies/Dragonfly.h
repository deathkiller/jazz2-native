#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Dragonfly
		
		Buzzing insect that bobs in place until a player comes near, then flies to a point past and
		above the player, hovers there following the player around, charges at them and flies home.
		Stays above the water surface. Defeated by a single hit.
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
		std::int32_t _phase;
		std::int32_t _wobbleTime;
		bool _behind;
		Vector2f _originPos;
		Vector2f _basePos;
		// In flight to the player it's the approach step, then the charge and the way home
		Vector2f _speedPerTick;
		std::shared_ptr<AudioBufferPlayer> _noise;

		void AdvanceTick();
		void StartNoise();
		void StopNoise();
	};
}