#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Dragon
		
		Small dragon that stays in place and faces the nearest player within range, then spits a stream
		of short-lived fireballs at them before pausing between bursts. Defeated by a single hit.
	*/
	class Dragon : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Dragon();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	public:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		// Public only so level scripts can tell the flame apart, see Scripting::LevelScriptLoader::OnActorAdded()
		class Fire : public EnemyBase
		{
			DEATH_RUNTIME_OBJECT(EnemyBase);

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

		private:
			float _timeLeft;
		};
#endif

	private:
		static constexpr float DefaultSpeed = 0.7f;

		/** @brief Time it stands still before it turns around, in original ticks */
		static constexpr float IdleTicks = 280.0f;
		/** @brief Idle time left under which it can attack, in original ticks */
		static constexpr float AttackReadyTicks = 140.0f;
		/** @brief Idle time it gets after an attack, in original ticks */
		static constexpr float AttackPauseTicks = 210.0f;
		/** @brief Number of flames in a burst and the time between them, in original ticks */
		static constexpr std::int32_t BurstFlames = 2;
		static constexpr float BurstSpacingTicks = 3.0f;
		/** @brief Ticks before the first flame of an attack and ticks the flames take, 32 together as measured in JJ2+ */
		static constexpr float AttackWindUpTicks = 27.0f;
		static constexpr float AttackTicks = 5.0f;

		// Attack time left while attacking, idle time left otherwise
		float _stateTime;
		float _attackTime;
		std::uint8_t _burstLeft;
		bool _attacking;

		void Turn(bool willFaceLeft);
	};
}