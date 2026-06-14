#pragma once

#include "../ActorBase.h"
#include "../../Direction.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Base class of an enemy
		
		Base for hostile creatures that hurt the player on contact. It provides the behavior shared by all enemies:
		collisions with the player and player shots, taking damage, freezing, awarding score, dropping random
		collectibles on death, and the white-mask hit-blinking effect.
	*/
	class EnemyBase : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		EnemyBase();

		/** @brief Whether the enemy should collide with player shots */
		bool CanCollideWithShots;

		bool OnHandleCollision(ActorBase* other) override;
		bool CanCauseDamage(ActorBase* collider) override;

		/** @brief Whether the enemy can hurt player */
		bool CanHurtPlayer() const {
			return (_canHurtPlayer && _frozenTimeLeft <= 0.0f);
		}

		/** @brief Whether the enemy is currently frozen */
		bool IsFrozen() const {
			return (_frozenTimeLeft > 0.0f);
		}

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Hide these members from documentation before refactoring
		bool _canHurtPlayer;
		std::uint32_t _scoreValue;
#endif

		void OnUpdate(float timeMult) override;
		void OnHealthChanged(ActorBase* collider) override;
		bool OnPerish(ActorBase* collider) override;

		/** @brief Awards the enemy's score value to the collider (or its owner) */
		void AddScoreToCollider(ActorBase* collider);
		/** @brief Sets the enemy's health scaled by the current game difficulty */
		virtual void SetHealthByDifficulty(std::int32_t health);
		/** @brief Snaps the enemy onto the ground below it */
		void PlaceOnGround();
		/** @brief Returns whether the enemy can move to the specified relative offset */
		bool CanMoveToPosition(float x, float y);
		/** @brief Randomly spawns a drop (collectible) at the enemy's position */
		void TryGenerateRandomDrop();
		/** @brief Starts the white-mask blinking effect */
		void StartBlinking();
		/** @brief Advances the blinking effect */
		void HandleBlinking(float timeMult);

	private:
		float _blinkingTimeout;
	};
}