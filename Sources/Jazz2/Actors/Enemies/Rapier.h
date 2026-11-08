#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Rapier

		Ghostly sword-bearing enemy that floats in a figure-eight around its origin, moaning occasionally.
		From time to time it winds up and lunges horizontally at the nearest player within range, brakes,
		and flies straight back to its origin. Takes a couple of hits and dissolves when defeated.
	*/
	class Rapier : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief State of the rapier */
		enum class RapierState : std::uint8_t {
			Floating,		/**< Floating around its origin */
			WindingUp,		/**< Preparing to lunge */
			Lunging,		/**< Accelerating toward the player */
			Braking,		/**< Slowing down after the lunge */
			Returning		/**< Flying straight back to its origin */
		};

		/** @brief Creates a new instance */
		Rapier();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		/** @brief Returns the current state */
		RapierState GetRapierState() const {
			return _rapierState;
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		Vector2f _originPos, _returnFrom, _speedPerTick;
		RapierState _rapierState;
		std::int32_t _counter;
		std::int32_t _floatCounter;
		float _tickTime;
		float _noiseCooldown;

		void AdvanceTick();
		void ChangeState(RapierState state);
		void ShowFrame(AnimState anim, std::int32_t frame);
		bool FindTarget(Vector2f& targetPos);
		Vector2f GetFloatPos() const;
	};
}
