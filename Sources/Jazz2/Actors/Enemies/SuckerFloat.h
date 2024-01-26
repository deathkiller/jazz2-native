#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class SuckerFloat : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		SuckerFloat();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		float _phase;
		Vector2i _originPos;
	};
}