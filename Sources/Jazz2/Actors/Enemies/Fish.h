#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Fish

		Underwater enemy that swims in a figure-eight around its starting spot and from time to time
		dashes at a player within range, overshooting, before swimming straight back. Doesn't move when
		it's not under the water and is defeated by a single hit.
	*/
	class Fish : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Fish();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		static constexpr std::int32_t StateSwimming = 0;
		static constexpr std::int32_t StateDashing = 1;
		static constexpr std::int32_t StateReturning = 2;

		std::int32_t _state;
		std::int32_t _counter;
		std::int32_t _swimCounter;
		float _tickTime;
		Vector2f _originPos;
		Vector2f _returnFrom;
		Vector2f _speedPerTick;

		void AdvanceTick();
		Vector2f GetSwimPos() const;
	};
}
