#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Bee

		Buzzing insect that circles its origin until a player comes within range, then swarms around them,
		steering its speed toward their position and overshooting, before flying straight back to its starting
		point and attacking again. Defeated by a single hit.
	*/
	class Bee : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief State of the bee */
		enum class BeeState : std::uint8_t {
			Idle,			/**< Waiting at its origin */
			Attacking,		/**< Swarming around the nearest player */
			Returning		/**< Flying straight back to its origin */
		};

		/** @brief Creates a new instance */
		Bee();
		~Bee();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		/** @brief Returns the current state */
		BeeState GetBeeState() const {
			return _beeState;
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		std::shared_ptr<AudioBufferPlayer> _noise;
		Vector2f _originPos, _returnFrom, _speedPerTick;
		std::int32_t _counter;
		float _tickTime;
		BeeState _beeState;

		void AdvanceTick();
		bool FindTarget(Vector2f& targetPos, float maxDistanceSqr) const;
		void StartNoise();
		void StopNoise();
	};
}
