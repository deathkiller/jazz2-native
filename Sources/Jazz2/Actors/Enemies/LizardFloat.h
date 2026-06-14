#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Environment
{
	class Copter;
}

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Floating lizard
		
		Lizard suspended from a copter that hovers in the air, drifting toward nearby players and
		lobbing bombs at them. When defeated by an ordinary hit it drops its copter and falls to the
		ground as a normal walking @ref Lizard; only special moves, freezing, or heavy weapons destroy
		it outright.
	*/
	class LizardFloat : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		LizardFloat();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnTileDeactivated() override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 1.4f;

		float _attackTime;
		float _moveTime;
		uint8_t _theme;
		uint8_t _copterDuration;
		std::shared_ptr<Environment::Copter> _copter;
	};
}