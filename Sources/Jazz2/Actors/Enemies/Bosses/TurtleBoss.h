#pragma once

#include "BossBase.h"

#include <memory>

namespace Jazz2::Actors::Bosses
{
	/**
		@brief Turtle (boss)

		The Tuf Turtle boss, an oversized turtle that walks toward the nearest player. Once it has walked
		for a while with a player nearby it stops to swing a spiked mace that flies out to where the player
		stood and returns. It is defeated by depleting its health, after which its shell is flung loose.
	*/
	class TurtleBoss : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		/** @brief Creates a new instance */
		TurtleBoss();
		~TurtleBoss();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		bool TakesSingleBlastDamage() const override {
			return true;
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnActivatedBoss() override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr std::int32_t StateWaiting = 0;
		static constexpr std::int32_t StateWalking = 1;
		static constexpr std::int32_t StateAttacking = 2;

		/** @brief Walking speed in pixels per frame (1.5 pixels per original tick) */
		static constexpr float WalkSpeed = 1.5f * TicksPerFrame;
		/** @brief Squared distance in pixels within which players are noticed (512 px) */
		static constexpr float NoticeDistanceSqr = 262144.0f;
		/** @brief Ticks of walking before an attack can start */
		static constexpr float AttackMinTicks = 70.0f;
		/** @brief Period in ticks of turning toward the nearest player */
		static constexpr float TurnPeriodTicks = 8.0f;
		/** @brief Period in ticks of testing whether to attack */
		static constexpr float AttackPeriodTicks = 4.0f;
		/** @brief Ticks per frame of the attack animation */
		static constexpr float AttackFrameTicks = 5.0f;
		/** @brief Ticks from the start of the attack to the throw of the mace (15 frames) */
		static constexpr float ThrowTicks = 15.0f * AttackFrameTicks;
		/** @brief Ticks from the start of the attack to the end of the animation (19 frames) */
		static constexpr float AttackAnimTicks = 19.0f * AttackFrameTicks;
		/** @brief Ticks of the attack end animation, measured in JJ2+ */
		static constexpr float AttackEndTicks = 19.0f;
		/** @brief Horizontal offset in pixels of the mace when it returns next to the boss */
		static constexpr float MaceReturnOffset = 16.0f;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class Mace : public EnemyBase
		{
			DEATH_RUNTIME_OBJECT(EnemyBase);

		public:
			~Mace();

			/** @brief Sets the owner, the aim offset in pixels and the difficulty index */
			void Setup(const std::shared_ptr<ActorBase>& owner, Vector2f aim, std::int32_t difficulty);

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;

		private:
			/** @brief Clock value at which the mace is back at its start */
			static constexpr float OutAndBackClock = 128.0f;
			/** @brief Clock value after which the mace disappears */
			static constexpr float EndClock = 137.0f;

			Vector2f _originPos;
			Vector2f _aim;
			float _clock;
			float _clockStep;
			std::weak_ptr<ActorBase> _owner;
#if defined(WITH_AUDIO)
			std::shared_ptr<AudioBufferPlayer> _sound;
#endif
		};
#endif

		std::int32_t _state;
		std::uint8_t _endText;
		float _walkTicks;
		float _turnAcc;
		float _attackAcc;
		float _attackTicks;
		float _attackEndStart;
		bool _maceThrown;
		bool _aimCaptured;
		Vector2f _aimPos;
		std::weak_ptr<ActorBase> _target;
		std::shared_ptr<Mace> _mace;

		void StartWalking();
		void StartAttack(Player* target);
		void UpdateWalking(float timeMult);
		void UpdateAttacking(float timeMult);
	};
}
