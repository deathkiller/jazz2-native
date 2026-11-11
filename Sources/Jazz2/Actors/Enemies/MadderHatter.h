#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Madder hatter
		
		Top-hatted enemy that strolls back and forth, turning at walls and ledges, and faces nearby
		players to spit a fast projectile from its drinking cup. Takes several hits to defeat and
		scatters its hat and cup as debris when destroyed.
	*/
	class MadderHatter : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		MadderHatter();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		float GetTurnLookAhead() const override {
			return 31.0f;
		}
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		static constexpr float DefaultSpeed = 0.76f * TicksPerFrame;
		// Measured in JJ2+: it walks for 70 ticks between attacks
		static constexpr float WalkTicks = 70.0f;
		/** @brief Distance within which it spits at a player */
		static constexpr float SpitDistance = 128.0f;
		/** @brief Most extra attack loops that can follow the first one */
		static constexpr std::int32_t MaxExtraLoops = 3;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class BulletSpit : public EnemyBase
		{
			DEATH_RUNTIME_OBJECT(EnemyBase);

		public:
			bool OnHandleCollision(ActorBase* other) override;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;
			bool OnPerish(ActorBase* collider) override;
			void OnHitFloor(float timeMult) override;
			void OnHitWall(float timeMult) override;
			void OnHitCeiling(float timeMult) override;

		private:
			float _timeLeft;
		};
#endif

		float _attackTime;
		bool _stuck;
		std::uint8_t _extraLoops;

		void Idle(float timeMult);
		void Walking(float timeMult);
		void Attack(std::int32_t loop);
	};
}