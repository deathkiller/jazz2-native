#pragma once

#include "IAudioDevice.h"
#include "../Base/Algorithms.h"
#include "../Primitives/Vector3.h"

#include <cmath>
#include <cstdint>

namespace nCine::AudioMixer
{
	inline float Clamp01(float value)
	{
		return (value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value));
	}

	/**
		@brief Saturates a mixed sample to 16 bits
		
		Clipping instead of wrapping, which would invert the waveform's polarity.
	*/
	inline std::int16_t ClampToInt16(std::int32_t value)
	{
		return std::int16_t(value < -32768 ? -32768 : (value > 32767 ? 32767 : value));
	}

	/**
		@brief Per-output-frame step through a source's samples, as a 32.32 fixed-point cursor
		
		@param sourceFrequency  Sample rate of the buffer being read
		@param mixFrequency     Sample rate the backend is mixing at
		@param pitch            Voice pitch, `1.0` for none
	*/
	inline std::int64_t ComputeResampleStep(std::int32_t sourceFrequency, std::int32_t mixFrequency, float pitch)
	{
		// Uses double, which is soft-float on MIPS/PowerPC consoles; an integer form would be faster
		// but is not bit-identical, so it needs testing on real hardware first
		return std::int64_t((double(sourceFrequency) / double(mixFrequency)) * double(pitch) * 4294967296.0);
	}

	/**
		@brief Per-voice stereo gains of a software-mixing backend
		
		Matches the OpenAL distance model (`AL_LINEAR_DISTANCE_CLAMPED` between @ref IAudioDevice::ReferenceDistance
		and @ref IAudioDevice::MaxDistance) with constant-power panning. @p position is in physical units,
		@p listenerPosition in pixels, so it's converted here. Relative sources are used as they are.
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
		const float distance = sqrtApprox(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z);

		float attenuation = 1.0f;
		if (distance > IAudioDevice::ReferenceDistance) {
			const float clamped = (distance < IAudioDevice::MaxDistance ? distance : IAudioDevice::MaxDistance);
			attenuation = 1.0f - (clamped - IAudioDevice::ReferenceDistance) / (IAudioDevice::MaxDistance - IAudioDevice::ReferenceDistance);
		}

		const float pan = (distance > 0.0001f ? Clamp01(0.5f + 0.5f * (delta.X / distance)) : 0.5f);
		const float gain = gain0 * attenuation;
		leftGain = gain * sqrtApprox(1.0f - pan);
		rightGain = gain * sqrtApprox(pan);
	}
}
