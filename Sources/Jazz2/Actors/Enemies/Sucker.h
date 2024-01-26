#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class Sucker : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Sucker();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		int _cycle;
		float _cycleTimer;
		bool _stuck;
	};
}