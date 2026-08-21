#include "RemoteElectroShot.h"

#if defined(WITH_MULTIPLAYER)

#include "../Weapons/ElectroShot.h"

namespace Jazz2::Actors::Multiplayer
{
	RemoteElectroShot::RemoteElectroShot()
		: _poweredUp(false), _currentStep(0.0f),
			// The owning shot starts the swirl only after it has snapped to the gunspot (2 frames after firing)
			_particleSpawnTime(2.0f)
	{
		// The sprite stays hidden, but the particle trail follows the position, so it has to keep interpolating
		_alwaysInterpolate = true;
	}

	void RemoteElectroShot::AssignMetadata(std::uint8_t flags, ActorState state, StringView path, AnimState anim, float rotation, float scaleX, float scaleY, ActorRendererType rendererType)
	{
		RemoteActor::AssignMetadata(flags, state, path, anim, rotation, scaleX, scaleY, rendererType);

		_poweredUp = (anim == Weapons::ElectroShot::PoweredUpAnimState);
	}

	void RemoteElectroShot::OnUpdate(float timeMult)
	{
		RemoteActor::OnUpdate(timeMult);

		// Mirror the emitter of the owning shot at the synced position; the spawned particles then live out
		// their fixed lifetime in the tilemap like on the server
		_particleSpawnTime -= timeMult;
		if (_particleSpawnTime <= 0.0f) {
			_particleSpawnTime += 1.0f;
			Weapons::ElectroShot::CreateParticles(_levelHandler, _metadata, _pos, _renderer.layer(),
				_currentStep, IsFacingLeft(), _poweredUp);
		}

		_currentStep += timeMult;
	}

	void RemoteElectroShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		Weapons::ElectroShot::EmitParticleLight(lights, _pos, _currentStep);
	}
}

#endif
