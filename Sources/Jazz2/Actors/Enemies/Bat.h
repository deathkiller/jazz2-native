#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class Bat : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Bat();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = -1.0f;

		Vector2f _originPos;
		bool _attacking;
		float _noiseCooldown;

		bool FindNearestPlayer(Vector2f& targetPos);
	};
}