#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class Witch : public EnemyBase
	{
	public:
		Witch();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		bool OnTileDeactivate(int tx1, int ty1, int tx2, int ty2) override;

	private:
		static constexpr float DefaultSpeed = -4.0f;

		class MagicBullet : public ActorBase
		{
		public:
			MagicBullet(Witch* owner) : _owner(owner), _time(380.0f) { }

			bool OnHandleCollision(ActorBase* other) override;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights);

		private:
			Witch* _owner;
			float _time;

			void FollowNearestPlayer();
		};

		float _attackTime;
		bool _playerHit;

		void OnPlayerHit();
	};
}