#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Sucker
		
		Small plunger-like creature that shuffles along the ground in short bursts, popping and squeaking
		as it moves and turning at walls and ledges. It harms the player only on contact and is defeated
		by a single hit; it can also spawn deflating from a destroyed @ref SuckerFloat.
	*/
	class Sucker : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Sucker();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		std::int32_t _cycle;
		float _cycleTimer;
		bool _stuck;
	};
}