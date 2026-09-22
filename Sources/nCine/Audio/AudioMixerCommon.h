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
		@brief Saturates a mixed sample into the 16 bits every software backend hands its hardware

		The accumulator a software mixer sums voices into is wider than the output on purpose, because with
		a dozen voices a loud moment genuinely exceeds the range. Wrapping the excess would invert the
		waveform's polarity, which is a far worse artefact than the clipping this does instead.

		Shared so that a change to the saturation policy - a soft knee, dither, a bit of headroom - is one
		edit rather than one per backend, which is how the mixers have drifted before.
	*/
	inline std::int16_t ClampToInt16(std::int32_t value)
	{
		return std::int16_t(value < -32768 ? -32768 : (value > 32767 ? 32767 : value));
	}

	/**
		@brief Per-output-frame step through a source's samples, as a 32.32 fixed-point cursor

		How far the read cursor advances for each frame the mixer writes: the ratio of the source's own rate
		to the rate being mixed at, scaled by the voice's pitch. The 32.32 form is what lets the inner loop
		advance with an integer add and take the interpolation fraction straight out of the low word.

		Every software backend resamples this way, so it lives here rather than being spelled out (with its
		@cpp 4294967296.0 @ce) in each of them.

		@param sourceFrequency  Sample rate of the buffer being read
		@param mixFrequency     Sample rate the backend is mixing at
		@param pitch            Voice pitch, `1.0` for none
	*/
	inline std::int64_t ComputeResampleStep(std::int32_t sourceFrequency, std::int32_t mixFrequency, float pitch)
	{
		// Note for anyone profiling a console: this is `double` arithmetic in a per-voice, per-block path,
		// and the MIPS and PowerPC consoles have no hardware double - it lowers to libgcc soft-float calls
		// there. An integer form ((sourceFrequency << 32) / mixFrequency, with pitch applied as 16.16) would
		// be both faster and more accurate at pitch 1.0, but it does not produce bit-identical steps, so it
		// wants an A/B against real hardware rather than a blind swap. Doing it here is now a one-line
		// change instead of six.
		return std::int64_t((double(sourceFrequency) / double(mixFrequency)) * double(pitch) * 4294967296.0);
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
