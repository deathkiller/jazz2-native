#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Environment
{
	class Copter;
}

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Floating lizard
		
		Lizard suspended from a copter that hovers in the air, drifting toward nearby players and
		lobbing bombs at them. When defeated by an ordinary hit it drops its copter and falls to the
		ground as a normal walking @ref Lizard; only special moves, freezing, or heavy weapons destroy
		it outright.
	*/
	class LizardFloat : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		LizardFloat();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnTileDeactivated() override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		/** @brief Distance within which a player wakes it up, as measured in JJ2+ */
		static constexpr float WakeDistance = 128.0f;
		/** @brief Distance within which a player makes it face them and wind up an attack */
		static constexpr float AttackDistance = 128.0f;
		/** @brief Time near a player before it attacks, in original ticks */
		static constexpr float AttackWaitTicks = 71.0f;
		/** @brief Horizontal distance to the player it tries to keep */
		static constexpr float KeepDistanceX = 100.0f;
		/** @brief Height above the player up to which it doesn't climb any higher */
		static constexpr float HoverHeight = 80.0f;
		/** @brief Acceleration in px per original tick squared */
		static constexpr float Acceleration = (2048.0f / 65536.0f) * TicksPerFrame * TicksPerFrame;
		/** @brief Share of the speed kept each original tick when not accelerating */
		static constexpr float SpeedKeptPerTick = 31.0f / 32.0f;
		/** @brief Copter flight time when the event gives none, in seconds */
		static constexpr std::uint8_t DefaultCopterDuration = 4;

		float _attackTime;
		float _moveTime;
		std::shared_ptr<Environment::Copter> _copter;
		Vector2f _speedBeforeAttack;
		std::uint8_t _targetIndex;
		uint8_t _theme;
		uint8_t _copterDuration;
		bool _awake;
		bool _attacking;
		bool _dropsCopter;
	};
}