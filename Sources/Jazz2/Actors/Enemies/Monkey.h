#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class Monkey : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Monkey();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnAnimationFinished() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 1.5f;

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

		bool _isWalking;
		bool _stuck;
	};
}