#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Helmut
		
		Helmeted creature that plods slowly along the ground, alternating between walking and pausing to
		idle, and reverses direction at walls and ledges. It has no special attack and harms the player
		only on contact; defeated by a single hit.
	*/
	class Helmut : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Helmut();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		float GetTurnLookAhead() const override {
			return 13.0f;
		}
		// Measured in JJ2+: it ignores Stop Enemy left of where it started
		bool StopsAtStopEnemy() const override {
			return _pos.X > _originX;
		}
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		static constexpr float DefaultSpeed = 0.91f * TicksPerFrame;

		float _stateTime;
		float _originX;
		bool _idling;
		bool _stuck;
	};
}