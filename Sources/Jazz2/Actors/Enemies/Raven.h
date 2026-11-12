#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Raven

		Black bird that drifts up and down near its perch until a player comes within range, then sweeps
		over the player from one side to the other, glides down at them and swoops back to its perch,
		staying clear of the water. Takes a couple of hits to defeat.
	*/
	class Raven : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Raven();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		enum class RavenState : std::uint8_t {
			Perched,
			Patrolling,
			Gliding,
			Swooping
		};

		Vector2f _originPos, _speedPerTick;
		std::int32_t _counter;
		float _tickTime;
		std::int32_t _angle;
		std::int32_t _angleStep;
		RavenState _ravenState;

		void AdvanceTick();
		void ChangeState(RavenState state);
		bool FindTarget(Vector2f& targetPos, float maxDistanceSqr);
		void Face(bool left);
	};
}
