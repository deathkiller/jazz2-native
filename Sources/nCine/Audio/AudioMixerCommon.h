#pragma once

#include "IAudioDevice.h"
#include "../Primitives/Vector3.h"

#include <cmath>

namespace nCine::AudioMixer
{
	inline float Clamp01(float value)
	{
		return (value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value));
	}

	/**
		@brief Per-voice stereo gains of a software-mixing backend

		Shared by every backend that mixes on the CPU (PS3, N64, Amiga, SDL, PSP), so the positional model
		sounds the same on all of them - and the same as the OpenAL backend, whose distance model this is:
		`AL_LINEAR_DISTANCE_CLAMPED` with a rolloff factor of 1 between @ref IAudioDevice::ReferenceDistance
		and @ref IAudioDevice::MaxDistance, with constant-power panning across the X axis (the only one a 2D
		game's stereo field uses).

		The frames matter. A player hands its backend a position in the OpenAL convention (see
		`IAudioPlayer::getAdjustedPosition()`): physical units, i.e. pixels times
		@ref IAudioDevice::LengthToPhysical with Y and Z negated, and for a positional source in WORLD space
		- the listener was subtracted for the near-field smoothing and added back. The listener itself arrives
		through `updateListener()` in raw pixels, so it is brought into the same frame here before the
		difference is taken. A relative source (UI sounds, music, the 2D panning vector of length 1) is
		already head-locked and is used as it is. Getting this wrong is not subtle: with the listener left in
		pixels every positional sound went silent as soon as the camera was a few hundred pixels from the
		level's origin.
	*/
	inline void ComputeStereoGains(bool relative, const Vector3f& position, const Vector3f& listenerPosition,
		float sourceGain, float masterGain, float& leftGain, float& rightGain)
	{
		const float gain0 = Clamp01(sourceGain) * Clamp01(masterGain);

		Vector3f delta = position;
		if (!relative) {
			delta -= Vector3f(listenerPosition.X * IAudioDevice::LengthToPhysical,
				listenerPosition.Y * -IAudioDevice::LengthToPhysical, listenerPosition.Z * -IAudioDevice::LengthToPhysical);
		}
		const float distance = std::sqrt(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z);

		float attenuation = 1.0f;
		if (distance > IAudioDevice::ReferenceDistance) {
			const float clamped = (distance < IAudioDevice::MaxDistance ? distance : IAudioDevice::MaxDistance);
			attenuation = 1.0f - (clamped - IAudioDevice::ReferenceDistance) / (IAudioDevice::MaxDistance - IAudioDevice::ReferenceDistance);
		}

		const float pan = (distance > 0.0001f ? Clamp01(0.5f + 0.5f * (delta.X / distance)) : 0.5f);
		const float gain = gain0 * attenuation;
		leftGain = gain * std::sqrt(1.0f - pan);
		rightGain = gain * std::sqrt(pan);
	}
}
