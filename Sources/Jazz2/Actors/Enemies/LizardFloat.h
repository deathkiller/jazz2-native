#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Environment
{
	class Copter;
}

namespace Jazz2::Actors::Enemies
{
	class LizardFloat : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		LizardFloat();

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