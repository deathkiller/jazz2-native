#pragma once

#include "BossBase.h"

namespace Jazz2::Actors::Bosses
{
	/**
		@brief Queen (boss)
		
		The Queen of Board, who stands on a ledge and screams to shove the player backwards while dropping
		bricks from above. She is invulnerable except briefly while screaming. She cannot be killed by normal
		damage; instead the player must use a spring to launch her off the ledge to defeat her.
	*/
	class Queen : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		/** @brief Creates a new instance */
		Queen();
		~Queen();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnActivatedBoss() override;
		void OnUpdate(float timeMult) override;

	private:
		static constexpr std::int32_t StateTransition = -1;
		static constexpr std::int32_t StateWaiting = 0;
		static constexpr std::int32_t StateIdleToScream = 1;
		static constexpr std::int32_t StateIdleToStomp = 2;
		static constexpr std::int32_t StateIdleToBackstep = 3;
		static constexpr std::int32_t StateDead = 4;
		static constexpr std::int32_t StateScreaming = 5;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class Brick : public EnemyBase
		{
		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			bool OnPerish(ActorBase* collider) override;

		private:
			float _timeLeft;
		};

		class InvisibleBlock : public ActorBase
		{
			friend class Queen;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
		};
#endif

		std::int32_t _state = StateWaiting;
		float _stateTime;
		std::uint8_t _endText;
		std::int32_t _lastHealth;
		bool _queuedBackstep;
		float _stepSize;
		Vector2f _originPos;
		std::shared_ptr<InvisibleBlock> _block;
	};
}