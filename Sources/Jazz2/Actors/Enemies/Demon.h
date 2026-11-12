#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Demon
		
		Ground-bound creature that idles until a player at about its height comes within range, then
		charges toward them, turning at walls and ledges, and slides to a stop once nobody is near.
		Takes a couple of hits to defeat.
	*/
	class Demon : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Demon();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		float GetTurnLookAhead() const override {
			return 25.0f;
		}
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		static constexpr float WalkSpeed = 1.45f;
		static constexpr float BrakeFactor = 0.875f;
		static constexpr float IdleTicks = 140.0f;
		static constexpr float MinWalkTicks = 140.0f;
		static constexpr float NoticeDistance = 256.0f;
		static constexpr float NoticeHeight = 64.0f;
		static constexpr float StopDistance = 96.0f;
		static constexpr std::int32_t IdleBellFrame = 5;

		float _attackTime;
		float _walkTime;
		std::int32_t _lastIdleFrame;
		bool _attacking;
		bool _stuck;
	};
}