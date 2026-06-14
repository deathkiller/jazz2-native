#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
	 * @brief Doggy
	 *
	 * Dog that trots back and forth along the ground, turning at walls and ledges. When shot it
	 * becomes enraged, barking and dashing at high speed for a time before calming down. Appears as a
	 * cat in the Secret Files episode. Takes several hits to defeat.
	 */
	class Doggy : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Doggy();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		bool OnHandleCollision(ActorBase* other) override;

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		float _attackSpeed;
		float _attackTime;
		float _noiseCooldown;
		bool _stuck;
	};
}