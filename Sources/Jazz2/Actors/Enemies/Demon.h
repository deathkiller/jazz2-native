#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
	 * @brief Demon
	 *
	 * Ground-bound creature that idles until a player comes within range, then lunges forward in a
	 * fast charging attack, turning at walls and ledges before returning to idle. Takes a couple of
	 * hits to defeat.
	 */
	class Demon : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Demon();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		float _attackTime;
		bool _attacking;
		bool _stuck;
		float _turnCooldown;
	};
}