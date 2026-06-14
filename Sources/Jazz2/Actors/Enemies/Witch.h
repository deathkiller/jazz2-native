#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
	 * @brief Witch
	 *
	 * Broom-riding witch that flies above the nearest player and casts homing magic bullets; a bullet
	 * that hits the player morphs them into a frog, after which she cackles and flies off. Far tougher
	 * than a common enemy, she takes many hits and dies with a dedicated death animation.
	 */
	class Witch : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Witch();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		bool OnTileDeactivated() override;

	private:
		static constexpr float DefaultSpeed = -4.0f;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class MagicBullet : public ActorBase
		{
			DEATH_RUNTIME_OBJECT(ActorBase);

		public:
			MagicBullet(Witch* owner) : _owner(owner), _time(380.0f) { }

			bool OnHandleCollision(ActorBase* other) override;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

		private:
			Witch* _owner;
			float _time;

			void FollowNearestPlayer();
		};
#endif

		float _attackTime;
		bool _playerHit;

		void OnPlayerHit();
	};
}