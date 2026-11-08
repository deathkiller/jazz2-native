#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Bat

		Hangs from its roost, twitching now and then, until a player comes within 125 px. It then takes off and
		flutters towards the player, sideways by a random step every tick and up or down by a new random speed every
		wing beat. When the player is more than 256 px from its roost, it flies back and hangs there again. All of it
		as measured in JJ2+.
	*/
	class Bat : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief State of the bat */
		enum class BatState : std::uint8_t {
			Hanging,	/**< Hangs from its roost */
			Waking,		/**< Takes off */
			Flying		/**< Flies towards the player or back to its roost */
		};

		/** @brief Creates a new instance */
		Bat();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		/** @brief Returns the current state */
		BatState GetBatState() const {
			return _batState;
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		// Distances and speeds in px of the original (per tick of 70 a second)
		static constexpr float WakeDistance = 125.0f;
		static constexpr float ChaseDistance = 256.0f;
		static constexpr float RoostDistance = 4.0f;
		static constexpr float MaxStepX = 1.5f;
		static constexpr float MaxSpeedY = 2.0f;
		static constexpr std::int32_t TicksPerAnimFrame = 8;
		static constexpr std::int32_t TwitchCycle = 224;
		static constexpr std::int32_t TwitchFrames = 7;
		static constexpr std::int32_t WakeTicks = 5;

		Vector2f _originPos;
		float _speedPerTickY;
		float _tickTime;
		float _noiseCooldown;
		std::int32_t _counter;
		std::int32_t _frame;
		BatState _batState;

		void AdvanceTick();
		bool FindTarget(Vector2f& targetPos, float maxDistance, const Vector2f& from) const;
	};
}
