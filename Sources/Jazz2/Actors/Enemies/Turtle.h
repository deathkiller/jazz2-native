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

	private:
		static constexpr float DefaultSpeed = 1.0f;

		std::uint8_t _theme;
		bool _isAttacking;
		bool _isTurning;
		bool _isWithdrawn;
		bool _isDodging;
		float _dodgeCooldown;

		void HandleTurn(bool isFirstPhase);
		void Attack();
		bool ShouldDodge() const;
	};
}