#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
	 * @brief Fencer
	 *
	 * Sword-wielding enemy that stands its ground until a player comes near, then faces them and
	 * lunges with a hopping thrust, springing either forward or backward unpredictably between
	 * attacks. Takes several hits to defeat.
	 */
	class Fencer : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Fencer();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		float _stateTime;

		bool FindNearestPlayer(Vector2f& targetPos);
	};
}