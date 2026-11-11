#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Fat chick
		
		Plump bird that waddles back and forth on the ground, turning at walls and ledges and homing in
		when a player draws close. When the player is right in front of it, it performs a short melee
		peck attack. Takes several hits to defeat.
	*/
	class FatChick : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		FatChick();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		static constexpr float DefaultSpeed = 0.9f;
		/** @brief Distance within which it turns toward a player (not on Easy) */
		static constexpr float TurnDistance = 80.0f;
		/** @brief Horizontal offset a player must exceed before it turns toward them */
		static constexpr float TurnDeadZone = 16.0f;
		/** @brief Distance within which it attacks a player in front of it */
		static constexpr float AttackDistance = 40.0f;
		/** @brief Time it walks before it can attack, in original ticks */
		static constexpr float AttackWaitTicks = 140.0f;

		bool _isAttacking;
		bool _stuck;
		float _attackCooldown;

		bool FindNearestPlayer(float maxDistance, Vector2f& targetPos) const;
		void Attack();
	};
}