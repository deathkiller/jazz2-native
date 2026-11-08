#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Raven

		Black bird that rises above its perch when a player comes within range and flies back and forth
		over them. From time to time it glides down at the player and then swoops back to its perch,
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
		RavenState _ravenState;

		void AdvanceTick();
		void ChangeState(RavenState state);
		bool FindTarget(Vector2f& targetPos, float maxDistanceSqr);
		void Face(bool left);
	};
}
