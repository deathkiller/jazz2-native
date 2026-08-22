#pragma once

#include "../Primitives/Vector3.h"

#include <cmath>

namespace nCine::AudioMixer
{
	/** @brief Distance past which a positional source is inaudible, in the engine's own units */
	constexpr float MaxAudibleDistance = 1000.0f;

	inline float Clamp01(float value)
	{
		return (value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value));
	}

	/**
		@brief Per-voice stereo gains of a software-mixing backend

		Constant-power panning across the X axis (the only one a 2D game's stereo field uses) with linear
		distance attenuation out to @ref MaxAudibleDistance. Shared by every backend that mixes on the CPU
		(PS3, N64), so the positional model sounds the same on all of them; a relative source is
		head-locked (UI sounds, music) and plays centred at full gain.
	*/
	inline void ComputeStereoGains(bool relative, const Vector3f& position, const Vector3f& listenerPosition,
		float sourceGain, float masterGain, float& leftGain, float& rightGain)
	{
		const float gain0 = Clamp01(sourceGain) * Clamp01(masterGain);
		if (relative) {
			leftGain = rightGain = gain0;
			return;
		}

		const Vector3f delta = position - listenerPosition;
		const float distance = std::sqrt(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z);
		const float attenuation = (distance >= MaxAudibleDistance ? 0.0f : 1.0f - (distance / MaxAudibleDistance));

		const float pan = (distance > 0.0f ? Clamp01((delta.X / MaxAudibleDistance) * 0.5f + 0.5f) : 0.5f);
		const float gain = gain0 * attenuation;
		leftGain = gain * std::sqrt(1.0f - pan);
		rightGain = gain * std::sqrt(pan);
	}
}
