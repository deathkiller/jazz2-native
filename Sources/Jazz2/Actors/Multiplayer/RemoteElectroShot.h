#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "RemoteActor.h"

namespace Jazz2::Actors::Multiplayer
{
	/**
		@brief Remote representation of the electro shot in online session

		The electro shot has no visible sprite of its own - the owning actor renders it by spawning a swirl of
		particles every frame, which the generic synchronization can't carry over. This representation replays
		the same particle emitter (and its light) locally at the synchronized position. The powered-up variant
		is carried in the synchronized animation state, see @ref Weapons::ElectroShot::PoweredUpAnimState.
	*/
	class RemoteElectroShot : public RemoteActor
	{
		DEATH_RUNTIME_OBJECT(RemoteActor);

	public:
		/** @brief Creates a new instance */
		RemoteElectroShot();

		void AssignMetadata(std::uint8_t flags, ActorState state, StringView path, AnimState anim, float rotation, float scaleX, float scaleY, ActorRendererType rendererType) override;

	protected:
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

	private:
		bool _poweredUp;
		float _currentStep;
		float _particleSpawnTime;
	};
}

#endif
