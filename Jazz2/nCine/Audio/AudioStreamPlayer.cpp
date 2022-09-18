#include "AudioStreamPlayer.h"
#include "../ServiceLocator.h"

#define NCINE_INCLUDE_OPENAL
#include "../CommonHeaders.h"

namespace nCine
{
	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	AudioStreamPlayer::AudioStreamPlayer()
		: IAudioPlayer(ObjectType::AUDIOSTREAM_PLAYER), audioStream_()
	{
	}

	AudioStreamPlayer::AudioStreamPlayer(const unsigned char* bufferPtr, unsigned long int bufferSize)
		: IAudioPlayer(ObjectType::AUDIOSTREAM_PLAYER), audioStream_(bufferPtr, bufferSize)
	{
	}

	AudioStreamPlayer::AudioStreamPlayer(const StringView& filename)
		: IAudioPlayer(ObjectType::AUDIOSTREAM_PLAYER), audioStream_(filename)
	{
	}

	AudioStreamPlayer::~AudioStreamPlayer()
	{
		if (state_ != PlayerState::Stopped) {
			audioStream_.stop(sourceId_);
		}

		// Force unregister to allow to destroy this player immediately
		IAudioDevice& device = theServiceLocator().audioDevice();
		device.unregisterPlayer(this);
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	bool AudioStreamPlayer::loadFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		if (state_ != PlayerState::Stopped)
			audioStream_.stop(sourceId_);

		const bool hasLoaded = audioStream_.loadFromMemory(bufferPtr, bufferSize);
		if (hasLoaded == false)
			return false;

		//setName(bufferName);
		return true;
	}

	bool AudioStreamPlayer::loadFromFile(const char* filename)
	{
		if (state_ != PlayerState::Stopped)
			audioStream_.stop(sourceId_);

		const bool hasLoaded = audioStream_.loadFromFile(filename);
		if (hasLoaded == false)
			return false;

		//setName(filename);
		return true;
	}

	void AudioStreamPlayer::play()
	{
		IAudioDevice& device = theServiceLocator().audioDevice();
		const bool canRegisterPlayer = (device.numPlayers() < device.maxNumPlayers());

		switch (state_) {
			case PlayerState::Initial:
			case PlayerState::Stopped:
			{
				if (canRegisterPlayer == false)
					break;

				const unsigned int source = device.nextAvailableSource();
				if (source == IAudioDevice::UnavailableSource) {
					LOGW("No more available audio sources for playing");
					return;
				}
				sourceId_ = source;

				// Streams looping is not handled at enqueued buffer level
				alSourcei(sourceId_, AL_LOOPING, AL_FALSE);

				alSourcef(sourceId_, AL_GAIN, gain_);
				alSourcef(sourceId_, AL_PITCH, pitch_);

				updateFilters();

				alSourcei(sourceId_, AL_SOURCE_RELATIVE, isSourceRelative_ ? AL_TRUE : AL_FALSE);
				alSource3f(sourceId_, AL_POSITION, position_.X * IAudioDevice::LengthToPhysical, position_.Y * -IAudioDevice::LengthToPhysical, position_.Z * -IAudioDevice::LengthToPhysical);
				alSourcef(sourceId_, AL_REFERENCE_DISTANCE, IAudioDevice::ReferenceDistance);
				alSourcef(sourceId_, AL_MAX_DISTANCE, IAudioDevice::MaxDistance);

				alSourcePlay(sourceId_);
				state_ = PlayerState::Playing;

				device.registerPlayer(this);
				break;
			}
			case PlayerState::Playing:
				break;
			case PlayerState::Paused:
			{
				if (canRegisterPlayer == false)
					break;

				updateFilters();

				alSourcePlay(sourceId_);
				state_ = PlayerState::Playing;

				device.registerPlayer(this);
				break;
			}
		}
	}

	void AudioStreamPlayer::pause()
	{
		switch (state_) {
			case PlayerState::Initial:
			case PlayerState::Stopped:
				break;
			case PlayerState::Playing:
			{
				alSourcePause(sourceId_);
				state_ = PlayerState::Paused;
				break;
			}
			case PlayerState::Paused:
				break;
		}
	}

	void AudioStreamPlayer::stop()
	{
		switch (state_) {
			case PlayerState::Initial:
			case PlayerState::Stopped:
				break;
			case PlayerState::Playing:
			case PlayerState::Paused:
			{
				// Stop the source then unqueue every buffer
				audioStream_.stop(sourceId_);
				// Detach the buffer from source
				alSourcei(sourceId_, AL_BUFFER, 0);
#if defined(OPENAL_FILTERS_SUPPORTED)
				if (filterHandle_ != 0) {
					alSourcei(sourceId_, AL_DIRECT_FILTER, 0);
				}
#endif
				sourceId_ = 0;
				state_ = PlayerState::Stopped;
				break;
			}
		}
	}

	void AudioStreamPlayer::setLooping(bool isLooping)
	{
		IAudioPlayer::setLooping(isLooping);

		audioStream_.setLooping(isLooping);
	}

	void AudioStreamPlayer::updateState()
	{
		if (state_ == PlayerState::Playing) {
			const bool shouldStillPlay = audioStream_.enqueue(sourceId_, isLooping_);
			if (!shouldStillPlay) {
				// Detach the buffer from source
				alSourcei(sourceId_, AL_BUFFER, 0);
#if defined(OPENAL_FILTERS_SUPPORTED)
				if (filterHandle_ != 0) {
					alSourcei(sourceId_, AL_DIRECT_FILTER, 0);
				}
#endif
				sourceId_ = 0;
				state_ = PlayerState::Stopped;
			}
		}
	}

}
