#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Monkey
		
		Monkey that either patrols the ground on foot or hangs in place, hurling bananas at nearby
		players that arc through the air and splatter on impact. The throw distance scales with how far
		the player is. Takes several hits to defeat.
	*/
	class Monkey : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Monkey();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnAnimationFinished() override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	public:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		// Public only so level scripts can tell the banana apart, see Scripting::LevelScriptLoader::OnActorAdded()
		class Banana : public EnemyBase
		{
			DEATH_RUNTIME_OBJECT(EnemyBase);

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdateHitbox() override;
			bool OnPerish(ActorBase* collider) override;
			void OnHitFloor(float timeMult) override;
			void OnHitWall(float timeMult) override;
			void OnHitCeiling(float timeMult) override;

		private:
			std::shared_ptr<AudioBufferPlayer> _soundThrow;
		};
#endif

	private:
		/** @brief Time a walking monkey goes without seeing a player before it stands still, in original ticks */
		static constexpr float WalkTicks = 280.0f;
		/** @brief Time a walking monkey stands still, in original ticks */
		static constexpr float StandTicks = 70.0f;
		/** @brief Time per difficulty step a standing monkey waits between throws, in original ticks */
		static constexpr float ThrowWaitTicks = 70.0f;
		/** @brief Distance within which it sees a player */
		static constexpr float SightDistance = 256.0f;

		// Walk time, stand time left or throw wait, they're never in use together
		float _time;
		bool _isWalking;
		bool _stuck;
		bool _standing;
		bool _hasStood;

		float GetWalkSpeed() const;
		float GetThrowInterval() const;
		bool HasPlayerInSight() const;
		bool TryThrow();
		void ThrowBanana(std::uint32_t distance, std::int32_t offsetX);
	};
}