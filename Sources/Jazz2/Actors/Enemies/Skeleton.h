#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Skeleton
		
		Walking skeleton that shambles back and forth along the ground, turning at walls and ledges and
		harming the player on contact. It shakes loose bones when hit and clatters apart into a pile of
		bones and a skull when defeated, which takes several hits.
	*/
	class Skeleton : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Skeleton();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnHealthChanged(ActorBase* collider) override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		static constexpr float DefaultSpeed = TicksPerFrame;
		/** @brief Fewest bones it bursts into, besides the skull */
		static constexpr std::int32_t MinBoneCount = 31;
		/** @brief Most bones it bursts into, besides the skull */
		static constexpr std::int32_t MaxBoneCount = 46;

		bool _stuck;
	};
}