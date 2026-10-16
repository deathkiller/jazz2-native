#pragma once

#include "BossBase.h"

namespace Jazz2::Actors::Bosses
{
	/**
		@brief Bolly (boss)
		
		A large flying mechanical boss with a trailing chain tail that hovers around the arena tracking the
		nearest player. It periodically stops to launch a volley of homing rockets before resuming flight,
		and is defeated by depleting its health while dodging the rockets.
	*/
	class Bolly : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		/** @brief Creates a new instance */
		Bolly();
		~Bolly();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		/** @brief Animation of the part below the body */
		static constexpr std::uint32_t BottomAnimation = 1;
		/** @brief Animation of a ball of the chain */
		static constexpr std::uint32_t ChainBallAnimation = 3;
		/** @brief Animation of a small piece of the chain between the balls */
		static constexpr std::uint32_t ChainPieceAnimation = 4;
		/** @brief Number of entries of the chain on difficulties below Hard */
		static constexpr std::int32_t NormalChainLength = 5 * 3;

		/** @brief Returns whether an entry of the chain is a ball, the others are the small pieces between them */
		static constexpr bool IsChainBall(std::int32_t index) {
			return (index % 3) == 2;
		}
		/**
		 * @brief Appends where the entries of a whole chain are, from the position of the boss
		 *
		 * @p chainPhase is how far the chain has swung, it starts at 0 and grows by 0.06 a frame. The level
		 * editor shows the boss this way, with its chain swinging.
		 */
		static void GetChainOffsets(float chainPhase, std::int32_t chainLength, SmallVectorImpl<Vector2f>& offsets);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnActivatedBoss() override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr std::int32_t StateTransition = -1;
		static constexpr std::int32_t StateWaiting = 0;
		static constexpr std::int32_t StateFlying = 1;
		static constexpr std::int32_t StateNewDirection = 2;
		static constexpr std::int32_t StatePrepairingToAttack = 3;
		static constexpr std::int32_t StateAttacking = 4;

		static constexpr std::int32_t HardChainLength = 6 * 3;
		// Distance from the start of the chain, and from an entry of the chain to the next one, in pixels
		static constexpr float ChainStartDistance = 30.0f;
		static constexpr float ChainBallSize = 14.0f;
		static constexpr float ChainPieceSize = 7.0f;
		static constexpr std::int32_t MaxChainLength = HardChainLength;
		/** @brief Entries of @ref _chain one segment occupies --- two small pieces and the ball that ends it */
		static constexpr std::int32_t ChainPiecesPerSegment = 3;
		/**
		 * @brief Health of a chain ball
		 *
		 * The chain is shootable --- reported as *"the Sonic boss should have a breakable chain"* --- and
		 * **one** is the figure, because the difficulty is entirely in *what* you have to hit rather than in
		 * how many times:
		 *
		 * - Only the **balls** take shots at all. The two small pieces between them are
		 *   @cpp CanCollideWithShots = false @ce, so a shot passes through them, and hitting the ball is a
		 *   matter of aim rather than of volume of fire.
		 * - Only the ball at the **free end** of what is left can be hit. Every ball further in stays
		 *   @cpp IsInvulnerable @ce, so firing into the middle of the chain does nothing at all --- the chain
		 *   cannot be cut, only shortened.
		 * - A ball takes its own two pieces with it, so the chain comes off a whole segment at a time.
		 *
		 * Reported rather than measured, and it cannot be measured here --- a boss is out of reach of the
		 * trajectory probe, which drives one player through one test level.
		 */
		static constexpr std::int32_t ChainBallHealth = 1;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class BollyPart : public EnemyBase
		{
			friend class Bolly;

		public:
			float Size;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			bool OnPerish(ActorBase* collider) override;
		};

		class Rocket : public EnemyBase
		{
			friend class Bolly;

		public:
			bool OnHandleCollision(ActorBase* other) override;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
			bool OnPerish(ActorBase* collider) override;
			void OnHitFloor(float timeMult) override;
			void OnHitWall(float timeMult) override;
			void OnHitCeiling(float timeMult) override;

		private:
			float _timeLeft;
		};
#endif

		std::int32_t _state;
		float _stateTime;
		uint8_t _endText;
		std::shared_ptr<BollyPart> _bottom;
		//std::shared_ptr<BollyPart> _turret;
		std::shared_ptr<BollyPart> _chain[MaxChainLength];
		Vector2f _originPos;
		float _noiseCooldown;
		std::int32_t _rocketsLeft;
		float _chainPhase;

		static Vector2f GetChainDirection(float chainPhase, std::int32_t index);
		//void UpdateTurret(float timeMult);
		void FollowNearestPlayer(std::int32_t newState, float time);
		void FireRocket();
	};
}