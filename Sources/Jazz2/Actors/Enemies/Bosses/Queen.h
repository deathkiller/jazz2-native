#pragma once

#include "BossBase.h"

namespace Jazz2::Actors::Bosses
{
	/**
		@brief Queen (boss)

		The Queen of Board, who stands on a ledge and screams to shove the player backwards while dropping
		bricks from above. She is shielded while idle, shots only count as hits while she acts. She cannot be
		killed by normal damage; after enough hits she steps back toward the edge of the ledge, falls off it
		onto a spring and is launched out of the level.
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
		static constexpr std::int32_t StateWaiting = 0;
		static constexpr std::int32_t StateIdle = 1;
		static constexpr std::int32_t StateScream = 2;
		static constexpr std::int32_t StateStomp = 3;
		static constexpr std::int32_t StateBackstep = 4;
		static constexpr std::int32_t StateLedge = 5;
		static constexpr std::int32_t StateFalling = 6;
		static constexpr std::int32_t StateFlung = 7;

		/** @brief Idle ticks after activation */
		static constexpr float FirstIdleTicks = 140.0f;
		/** @brief Idle ticks after a scream or a backstep */
		static constexpr float ShortIdleTicks = 70.0f;
		/** @brief Idle ticks after a stomp */
		static constexpr float LongIdleTicks = 210.0f;
		/** @brief Hits after which a backstep becomes likely */
		static constexpr std::int32_t HitsForBackstep = 3;
		/** @brief Horizontal distance in pixels left of the origin where a player wakes the idle countdown */
		static constexpr float RegionLeft = 480.0f;
		/** @brief Vertical distance in pixels above the origin where a player wakes the idle countdown */
		static constexpr float RegionTop = 160.0f;
		/** @brief Ticks of the scream */
		static constexpr float ScreamTicks = 96.0f;
		/** @brief Ticks of the stomp */
		static constexpr float StompTicks = 72.0f;
		/** @brief Squared distance in pixels within which the stomp drops a brick (256 px) */
		static constexpr float StompRangeSqr = 65536.0f;
		/** @brief Height in pixels above the origin from which bricks fall */
		static constexpr float BrickHeight = 160.0f;
		/** @brief Ticks of one backstep animation loop */
		static constexpr float BackstepLoopTicks = 40.0f;
		/** @brief Distance in pixels of the step back */
		static constexpr float BackstepDistance = 8.0f;
		/** @brief Ticks of the ledge animation */
		static constexpr float LedgeTicks = 138.0f;
		/** @brief Horizontal speed in pixels per frame when she starts to fall (4 pixels per original tick) */
		static constexpr float FallSpeedX = 4.0f * TicksPerFrame;
		/** @brief Gravity in pixels per frame squared (0.125 per original tick squared) */
		static constexpr float Gravity = 0.125f * TicksPerFrame * TicksPerFrame;
		/** @brief Distance in pixels at which she notices the spring */
		static constexpr float SpringRange = 48.0f;
		/** @brief Launch speed in pixels per frame (8 pixels per original tick) */
		static constexpr float LaunchSpeed = 8.0f * TicksPerFrame;
		/** @brief Ticks of the flight before she is gone */
		static constexpr float FlightTicks = 70.0f;
		/** @brief How far below her middle the ground under a step is looked for, in pixels */
		static constexpr float LedgeProbeDepth = 48.0f;
		/** @brief Distance in pixels of the invisible wall in front of her */
		static constexpr float WallDistance = 40.0f;

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

		std::int32_t _state;
		float _ticks;
		float _idleTicks;
		std::int32_t _hits;
		std::int32_t _backstepLoops;
		std::uint8_t _endText;
		Vector2f _originPos;
		std::shared_ptr<InvisibleBlock> _block;

		void SetShielded(bool value);
		bool IsPlayerInRegion();
		void ChooseAction();
		void StartScream();
		void StartStomp();
		void StartBackstep();
		void EnterIdle(float idleTicks);
		bool IsSolidAt(float x, float y);
		bool FindSpring();
	};
}
