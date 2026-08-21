#include "RemoteThunderbolt.h"

#if defined(WITH_MULTIPLAYER)

#include "../Weapons/Thunderbolt.h"

namespace Jazz2::Actors::Multiplayer
{
	RemoteThunderbolt::RemoteThunderbolt()
		: _lightProgress(0.0f), _muzzleFlash(false)
	{
	}

	void RemoteThunderbolt::AssignMetadata(std::uint8_t flags, ActorState state, StringView path, AnimState anim, float rotation, float scaleX, float scaleY, ActorRendererType rendererType)
	{
		RemoteActor::AssignMetadata(flags, state, path, anim, rotation, scaleX, scaleY, rendererType);

		// The first bolt of a burst is carried in the synced animation state (aliased beam states)
		_muzzleFlash = ((std::uint32_t)anim >= Weapons::Thunderbolt::InitialShotAnimOffset);
	}

	Vector2f RemoteThunderbolt::GetFarPoint() const
	{
		// Mirrors Thunderbolt::OnFire(): the beam extends a fixed distance from the muzzle along the synced
		// rotation, backwards when facing left
		float rotation = _renderer.rotation();
		float distance = (IsFacingLeft() ? -Weapons::Thunderbolt::BeamDistance : Weapons::Thunderbolt::BeamDistance);
		return Vector2f(_pos.X + cosf(rotation) * distance, _pos.Y + sinf(rotation) * distance);
	}

	void RemoteThunderbolt::OnUpdate(float timeMult)
	{
		RemoteActor::OnUpdate(timeMult);

		// The lifetime progress advances locally at the same rate as on the server, so the sparks and the
		// muzzle flash play out the same way for every observer
		if (_lightProgress < fPi) {
			Weapons::Thunderbolt::CreateSparks(_levelHandler, _metadata, _pos, GetFarPoint(), _renderer.layer(), timeMult);
		}

		_lightProgress += timeMult * Weapons::Thunderbolt::LightProgressSpeed;
	}

	void RemoteThunderbolt::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		Weapons::Thunderbolt::EmitBeamLights(lights, _pos, GetFarPoint(), _lightProgress, _muzzleFlash);
	}
}

#endif
