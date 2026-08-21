#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "RemoteActor.h"

namespace Jazz2::Actors::Multiplayer
{
	/**
		@brief Remote representation of the thunderbolt in online session

		The beam sprite itself is covered by the generic synchronization, but the sparks discharging off the
		beam and its lights (including the muzzle flash) are spawned locally by the owning actor, so they are
		replayed here along the beam reconstructed from the synchronized position and rotation.
	*/
	class RemoteThunderbolt : public RemoteActor
	{
		DEATH_RUNTIME_OBJECT(RemoteActor);

	public:
		/** @brief Creates a new instance */
		RemoteThunderbolt();

		void AssignMetadata(std::uint8_t flags, ActorState state, StringView path, AnimState anim, float rotation, float scaleX, float scaleY, ActorRendererType rendererType) override;

	protected:
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

	private:
		float _lightProgress;
		bool _muzzleFlash;

		Vector2f GetFarPoint() const;
	};
}

#endif
