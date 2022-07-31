#define NCINE_INCLUDE_OPENAL
#include "../CommonHeaders.h"

#include "IAudioPlayer.h"
#include "IAudioDevice.h"
#include "../Primitives/Vector3.h"

namespace nCine {

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	IAudioPlayer::IAudioPlayer(ObjectType type)
		: Object(type), sourceId_(IAudioDevice::UnavailableSource),
		state_(PlayerState::Stopped), isLooping_(false), isSourceRelative_(false),
		gain_(1.0f), pitch_(1.0f), lowPass_(1.0f), position_(0.0f, 0.0f, 0.0f),
		filterHandle_(0)
	{
	}

	IAudioPlayer::~IAudioPlayer()
	{
#if OPENAL_FILTERS_SUPPORTED
		if (filterHandle_ != 0) {
			alDeleteFilters(1, &filterHandle_);
			filterHandle_ = 0;
		}
#endif
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	int IAudioPlayer::sampleOffset() const
	{
		int byteOffset = 0;
		alGetSourcei(sourceId_, AL_SAMPLE_OFFSET, &byteOffset);
		return byteOffset;
	}

	void IAudioPlayer::setSampleOffset(int byteOffset)
	{
		alSourcei(sourceId_, AL_SAMPLE_OFFSET, byteOffset);
	}

	/*! The change is applied to the OpenAL source only when playing. */
	void IAudioPlayer::setSourceRelative(bool isSourceRelative)
	{
		if (isSourceRelative_ != isSourceRelative) {
			isSourceRelative_ = isSourceRelative;
			if (state_ == PlayerState::Playing) {
				alSourcei(sourceId_, AL_SOURCE_RELATIVE, isSourceRelative_ ? AL_TRUE : AL_FALSE);
			}
		}
	}

	/*! The change is applied to the OpenAL source only when playing. */
	void IAudioPlayer::setGain(float gain)
	{
		gain_ = gain;
		if (state_ == PlayerState::Playing) {
			alSourcef(sourceId_, AL_GAIN, gain_);
		}
	}

	/*! The change is applied to the OpenAL source only when playing. */
	void IAudioPlayer::setPitch(float pitch)
	{
		pitch_ = pitch;
		if (state_ == PlayerState::Playing) {
			alSourcef(sourceId_, AL_PITCH, pitch_);
		}
	}

	void IAudioPlayer::setLowPass(float value)
	{
		if (lowPass_ != value) {
			lowPass_ = value;
			if (state_ == PlayerState::Playing) {
				updateFilters();
			}
		}
	}

	/*! The change is applied to the OpenAL source only when playing. */
	void IAudioPlayer::setPosition(const Vector3f& position)
	{
		position_ = position;
		if (state_ == PlayerState::Playing) {
			alSource3f(sourceId_, AL_POSITION, position.X * IAudioDevice::LengthToPhysical, position.Y * -IAudioDevice::LengthToPhysical, position.Z * -IAudioDevice::LengthToPhysical);
		}
	}

	void IAudioPlayer::updateFilters()
	{
#if OPENAL_FILTERS_SUPPORTED
		if (lowPass_ < 1.0f) {
			if (filterHandle_ == 0) {
				alGenFilters(1, &filterHandle_);
				alFilteri(filterHandle_, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
				alFilterf(filterHandle_, AL_LOWPASS_GAIN, 1.0f);
			}
			if (filterHandle_ != 0) {
				alFilterf(filterHandle_, AL_LOWPASS_GAINHF, lowPass_);
				alSourcei(sourceId_, AL_DIRECT_FILTER, filterHandle_);
			}
		} else if (filterHandle_ != 0) {
			alFilterf(filterHandle_, AL_LOWPASS_GAINHF, 1.0f);
			alSourcei(sourceId_, AL_DIRECT_FILTER, 0);
		}
#endif
	}
}
