﻿#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/** @brief Witch */
	class Witch : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Witch();

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

			bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

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