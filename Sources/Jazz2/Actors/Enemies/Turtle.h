#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Turtle
		
		Walks back and forth on the ground, withdrawing into its shell to turn at walls and, on harder
		difficulties, to dodge incoming shots, and snapping at players that get directly in front of it.
		An ordinary hit knocks it out of its shell, leaving a sliding @ref TurtleShell behind; a Xmas
		variant is available.
	*/
	class Turtle : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Turtle();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		bool OnHandleCollision(ActorBase* other) override;

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		static constexpr float DefaultSpeed = TicksPerFrame;
		/** @brief Ticks per frame of its walk, measured in JJ2+ */
		static constexpr float WalkFrameTicks = 8.0f;
		/** @brief Ticks per frame when it ducks to turn around, and how far into its first frame it starts, measured in JJ2+ */
		static constexpr float TurnFrameTicks = 8.0f;
		static constexpr float TurnFrameElapsedTicks = 2.0f;
		/** @brief Ticks it takes to come back out when it turns around, measured in JJ2+ */
		static constexpr float TurnEndTicks = 47.0f;
		/** @brief Time it walks before it reacts to a player, in original ticks */
		static constexpr float ReadyTicks = 70.0f;
		/** @brief Distance within which it notices a player in front of it */
		static constexpr float SightDistance = 128.0f;
		/** @brief Largest vertical distance at which it notices a player */
		static constexpr float SightHeight = 64.0f;
		/** @brief Distance under which it bites */
		static constexpr float BiteDistance = 80.0f;
		/** @brief Distance over which it withdraws from a player who has just fired */
		static constexpr float WithdrawDistance = 90.0f;
		/** @brief Distance within which it stays in its shell, measured in JJ2+ */
		static constexpr float WithdrawHoldDistance = 150.0f;
		/** @brief Ticks per frame when it withdraws from a shot and comes back out, measured in JJ2+ */
		static constexpr float WithdrawFrameTicks = 4.0f;
		/** @brief How recently a player must have fired, in original ticks */
		static constexpr float FireWindowTicks = 20.0f;

		std::uint8_t _theme;
		bool _isAttacking;
		bool _isTurning;
		bool _isWithdrawn;
		bool _isDodging;
		float _dodgeCooldown;
		float _walkTime;

		void HandleTurn(bool isFirstPhase);
		void Attack();
		Player* FindFacedPlayer(float& distanceSqr) const;
		bool HasPlayerWithin(float distance) const;
	};
}