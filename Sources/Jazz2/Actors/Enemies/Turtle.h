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

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		static constexpr float DefaultSpeed = TicksPerFrame;
		/** @brief Ticks it takes to duck and to come back out when it turns around, measured in JJ2+ */
		static constexpr float TurnStartTicks = 6.0f;
		static constexpr float TurnEndTicks = 47.0f;
		/** @brief Time it walks before it reacts to a player, in original ticks */
		static constexpr float ReadyTicks = 70.0f;
		/** @brief Distance within which it notices a player in front of it */
		static constexpr float SightDistance = 128.0f;
		/** @brief Largest vertical distance at which it notices a player when facing right */
		static constexpr float SightHeight = 64.0f;
		/** @brief Distance under which it bites */
		static constexpr float BiteDistance = 80.0f;
		/** @brief Distance over which it withdraws from a player who has just fired */
		static constexpr float WithdrawDistance = 90.0f;
		/** @brief Distance within which it stays in its shell */
		static constexpr float WithdrawHoldDistance = 100.0f;
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