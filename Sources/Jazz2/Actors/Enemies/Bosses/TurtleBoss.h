#pragma once

#include "BossBase.h"

namespace Jazz2::Actors::Bosses
{
	/** @brief Turtle (boss) */
	class TurtleBoss : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		TurtleBoss();
		~TurtleBoss();

		static void Preload(const ActorActivationDetails& details);

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnActivatedBoss() override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr std::int32_t StateTransition = -1;
		static constexpr std::int32_t StateWaiting = 0;
		static constexpr std::int32_t StateWalking1 = 1;
		static constexpr std::int32_t StateWalking2 = 2;
		static constexpr std::int32_t StateAttacking = 3;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class Mace : public EnemyBase
		{
			DEATH_RUNTIME_OBJECT(EnemyBase);

		public:
			~Mace();

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;

		private:
			static constexpr float TotalTime = 60.0f;

			Vector2f _originPos;
			Vector2f _targetPos;
			bool _returning;
			float _returnTime;
			Vector2f _targetSpeed;
#if defined(WITH_AUDIO)
			std::shared_ptr<AudioBufferPlayer> _sound;
#endif

			void FollowNearestPlayer();
		};
#endif

		std::int32_t _state;
		float _stateTime;
		std::uint8_t _endText;
		Vector2f _originPos;
		std::shared_ptr<Mace> _mace;
		float _maceTime;

		void FollowNearestPlayer(std::int32_t newState, float time);
	};
}