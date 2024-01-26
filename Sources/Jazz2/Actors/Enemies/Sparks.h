#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class Sparks : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Sparks();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = -2.0f;
	};
}