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

	private:
		static constexpr float DefaultSpeed = 1.5f;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
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

		bool _isWalking;
		bool _stuck;
	};
}