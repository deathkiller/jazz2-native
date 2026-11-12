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

		/** @brief Returns the speed a player it hurts is thrown with, zero for a plain hit */
		virtual Vector2f GetContactThrowSpeed() const {
			return Vector2f::Zero;
		}
		/** @brief Returns `true` if a TNT blast takes only one point of health however close it is */
		virtual bool TakesSingleBlastDamage() const {
			return false;
		}

		/** @brief Whether the enemy can hurt player */
		bool CanHurtPlayer() const {
			return (_canHurtPlayer && _frozenTimeLeft <= 0.0f);
		}

		/** @brief Returns the score awarded for destroying the enemy */
		std::uint32_t GetScoreValue() const {
			return _scoreValue;
		}
		/** @brief Sets the score awarded for destroying the enemy */
		void SetScoreValue(std::uint32_t value) {
			_scoreValue = value;
		}

	protected:
		/** @brief Number of original 70 Hz game ticks in one 60 Hz frame, converts measured per-tick values */
		static constexpr float TicksPerFrame = 70.0f / 60.0f;
		/** @brief Default distance in front of the enemy's middle where it checks for a reason to turn */
		static constexpr float DefaultTurnLookAhead = 21.0f;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Hide these members from documentation before refactoring
		bool _canHurtPlayer;
		std::uint32_t _scoreValue;
#endif

		void OnUpdate(float timeMult) override;
		void OnHealthChanged(ActorBase* collider) override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

		/** @brief Awards the enemy's score value to the collider (or its owner) */
		void AddScoreToCollider(ActorBase* collider);
		/** @brief Sets the enemy's health scaled by the current game difficulty */
		virtual void SetHealthByDifficulty(std::int32_t health);
		/** @brief Snaps the enemy onto the ground below it */
		void PlaceOnGround();
		/** @brief Returns whether the enemy can move to the specified relative offset */
		bool CanMoveToPosition(float x, float y);
		/** @brief Returns `true` if the enemy turns around at @ref EventType::AreaStopEnemy, as measured in JJ2+ */
		virtual bool StopsAtStopEnemy() const {
			return true;
		}
		/** @brief Returns the distance in front of the enemy's middle where the floor and @ref EventType::AreaStopEnemy are checked */
		virtual float GetTurnLookAhead() const {
			return DefaultTurnLookAhead;
		}
		/** @brief Returns the nearest living player within the squared distance or `nullptr`, optionally with its squared distance */
		Player* FindNearestPlayer(float maxDistanceSqr, float* distanceSqr = nullptr) const;
		/** @brief Returns the difficulty as a number, 0 for Easy, 1 for Normal and 2 for Hard */
		std::int32_t GetDifficultyIndex() const;
		/** @brief Returns the original's ticks elapsed in the level, the same for every actor */
		std::int32_t GetElapsedTicks() const;
		/** @brief Returns @p pos kept above the water surface, for an enemy that can't fly into the water */
		Vector2f ClampAboveWater(Vector2f pos) const;
		/** @brief Returns the sine of an angle in 1024 steps per turn, as the original's table */
		static float Sin1024(std::int32_t angle);
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