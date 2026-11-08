#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Doggy
		
		Dog that trots back and forth along the ground, turning at walls and ledges. When it comes close
		to a player in front of it, it becomes enraged and runs back and forth barking until the player
		is far away. When shot, it turns to the shooter and dashes. Appears as a cat in the Secret Files
		episode. Takes several hits to defeat.
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

		/** @brief Returns `true` while it's enraged or dashes after being shot */
		bool IsAttacking() const {
			return (_enraged || _attackTime > 0.0f);
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		float GetTurnLookAhead() const override {
			return 34.0f;
		}
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		float _attackTime;
		float _noiseCooldown;
		bool _enraged;
		bool _stuck;

		void UpdateSpeed();
		bool IsPlayerInFront() const;
		bool IsPlayerFarAway() const;
	};
}