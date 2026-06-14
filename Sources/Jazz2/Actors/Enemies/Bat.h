#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Bat
		
		Hangs idle from a fixed spot until a player comes near, then wakes up and swoops in to attack,
		homing toward the target while avoiding the water surface and returning to its origin when the
		player escapes. Easily dispatched with a single hit.
	*/
	class Bat : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Bat();

		/** @brief Preloads all assets required by this actor */
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