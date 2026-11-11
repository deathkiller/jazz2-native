#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Witch
		
		Broom-riding witch that flies above the nearest player and casts homing magic bullets; a bullet
		that hits the player morphs them into a frog, after which she cackles and flies off. Far tougher
		than a common enemy, she takes many hits and dies with a dedicated death animation.
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
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		/** @brief Largest horizontal offset to the target that she follows, her speed is a third of it per original tick */
		static constexpr float FlyClampX = 8.0f;
		/** @brief Largest vertical offset to the target that she follows, her speed is a third of it per original tick */
		static constexpr float FlyClampY = 4.0f;
		/** @brief Width of the sideways sway of her target around the player */
		static constexpr float SwayAmplitude = 64.0f;
		/** @brief Time of one sway, in original ticks */
		static constexpr float SwayPeriodTicks = 1024.0f;
		/** @brief Half-width and half-height of the ellipse she flies along before a player comes near */
		static constexpr float OrbitRadiusX = 96.0f;
		static constexpr float OrbitRadiusY = 64.0f;
		/** @brief Time of one orbit, in original ticks */
		static constexpr float OrbitPeriodTicks = 256.0f;
		/** @brief Squared distance within which a player wakes her up */
		static constexpr float WakeDistanceSqr = 2.0f * 256.0f * 256.0f;
		/** @brief Lowest she flies below her starting point */
		static constexpr float MaxDropBelowOrigin = 96.0f;
		static constexpr float FlyAboveDistance = 96.0f;
		static constexpr float AttackDistance = 300.0f;
		static constexpr float AttackTicks = 200.0f;
		static constexpr float FirstAttackTicks = 140.0f;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class MagicBullet : public ActorBase
		{
			DEATH_RUNTIME_OBJECT(ActorBase);

		public:
			MagicBullet(const std::shared_ptr<Witch>& owner) : _owner(owner), _time(380.0f) { }

			bool OnHandleCollision(ActorBase* other) override;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

		private:
			std::weak_ptr<Witch> _owner;
			float _time;

			void FollowNearestPlayer();
		};
#endif

		float _attackTime;
		bool _playerHit;
		bool _awake;
		float _orbitPhase;
		Vector2f _originPos;

		void OnPlayerHit();
	};
}