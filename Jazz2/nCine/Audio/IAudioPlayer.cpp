#define NCINE_INCLUDE_OPENAL
#include "../CommonHeaders.h"

#include "IAudioPlayer.h"
#include "IAudioDevice.h"
#include "../Primitives/Vector3.h"

namespace nCine {

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	IAudioPlayer::IAudioPlayer(ObjectType type, const char* name)
		: Object(type, name), sourceId_(IAudioDevice::UnavailableSource),
		state_(PlayerState::Stopped), isLooping_(false),
		gain_(1.0f), pitch_(1.0f), lowPass_(1.0f), position_(0.0f, 0.0f, 0.0f)
	{
	}

	IAudioPlayer::IAudioPlayer(ObjectType type)
		: Object(type), sourceId_(IAudioDevice::UnavailableSource),
		state_(PlayerState::Stopped), isLooping_(false),
		gain_(1.0f), pitch_(1.0f), lowPass_(1.0f), position_(0.0f, 0.0f, 0.0f)
	{
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
	void IAudioPlayer::setGain(float gain)
	{
		gain_ = gain;
		if (state_ == PlayerState::Playing)
			alSourcef(sourceId_, AL_GAIN, gain_);
	}

	/*! The change is applied to the OpenAL source only when playing. */
	void IAudioPlayer::setPitch(float pitch)
	{
		pitch_ = pitch;
		if (state_ == PlayerState::Playing)
			alSourcef(sourceId_, AL_PITCH, pitch_);
	}

	void IAudioPlayer::setLowPass(float value)
	{
		lowPass_ = value;
//#if !defined(__EMSCRIPTEN__)
//		if (state_ == PlayerState::Playing) {
//			if (lowPass_ < 1.0f) {
//				if (filterHandle_ == 0) {
//					alGenFilters(1, &filterHandle_);
//					alFilteri(filterHandle_, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
//					alFilterf(filterHandle_, AL_LOWPASS_GAIN, 1.0f);
//				}
//				if (filterHandle_ != 0) {
//					alFilterf(filterHandle_, AL_LOWPASS_GAINHF, lowPass_);
//					alSourcei(sourceId_, AL_DIRECT_FILTER, filterHandle_);
//				}
//			}
//		}
//#endif
	}

	/*! The change is applied to the OpenAL source only when playing. */
	void IAudioPlayer::setPosition(const Vector3f& position)
	{
		position_ = position;
		if (state_ == PlayerState::Playing)
			alSourcefv(sourceId_, AL_POSITION, position_.Data());
	}

	/*! The change is applied to the OpenAL source only when playing. */
	void IAudioPlayer::setPosition(float x, float y, float z)
	{
		position_.Set(x, y, z);
		if (state_ == PlayerState::Playing)
			alSourcefv(sourceId_, AL_POSITION, position_.Data());
	}

}
