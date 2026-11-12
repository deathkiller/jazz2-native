#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Caterpillar
		
		Stationary, invulnerable creature that inhales and then exhales clouds of smoke; the smoke
		drifts upward and leaves the player dizzy on contact. It cannot be killed, only briefly
		disoriented by shooting it, which interrupts its attack.
	*/
	class Caterpillar : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Caterpillar();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		bool OnHandleCollision(ActorBase* other) override;

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		static constexpr std::int32_t StateIdle = 0;
		static constexpr std::int32_t StateInhaleStart = 1;
		static constexpr std::int32_t StateInhale = 2;
		static constexpr std::int32_t StateExhaleStart = 3;
		static constexpr std::int32_t StateExhale = 4;
		static constexpr std::int32_t StateExhaleEnd = 5;
		static constexpr std::int32_t StateDisoriented = 6;

		static constexpr AnimState AnimStateInhaleStart = (AnimState)1;
		static constexpr AnimState AnimStateInhale = (AnimState)2;
		static constexpr AnimState AnimStateExhaleStart = (AnimState)3;
		static constexpr AnimState AnimStateExhale = (AnimState)4;
		static constexpr AnimState AnimStateDisoriented = (AnimState)6;

		// Ticks of the original per frame, a puff shows each frame longer
		static constexpr std::int32_t FrameTicks = 8;
		static constexpr float ExhaleFrameTicks = 12.0f;
		static constexpr float DisorientedTicks = 350.0f;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class Smoke : public EnemyBase
		{
			DEATH_RUNTIME_OBJECT(EnemyBase);

		public:
			bool OnHandleCollision(ActorBase* other) override;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;

		private:
			float _time;
			Vector2f _baseSpeed;
		};
#endif

		std::int32_t _state;
		std::int32_t _count;
		float _time;

		void StartStep(std::int32_t state);
		std::int32_t ShowFrame(AnimState state, std::int32_t frame);
		void SpawnSmoke();
	};
}