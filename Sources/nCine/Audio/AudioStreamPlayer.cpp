#include "AudioStreamPlayer.h"
#include "../ServiceLocator.h"

#define NCINE_INCLUDE_OPENAL
#include "../CommonHeaders.h"

namespace nCine
{
	AudioStreamPlayer::AudioStreamPlayer()
		: IAudioPlayer(ObjectType::AudioStreamPlayer), audioStream_()
	{
	}

	/*AudioStreamPlayer::AudioStreamPlayer(const unsigned char* bufferPtr, unsigned long int bufferSize)
		: IAudioPlayer(ObjectType::AudioStreamPlayer), audioStream_(bufferPtr, bufferSize)
	{
	}*/

	AudioStreamPlayer::AudioStreamPlayer(const StringView& filename)
		: IAudioPlayer(ObjectType::AudioStreamPlayer), audioStream_(filename)
	{
	}

	AudioStreamPlayer::~AudioStreamPlayer()
	{
		stop();
	}

	/*bool AudioStreamPlayer::loadFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		if (state_ != PlayerState::Stopped) {
			audioStream_.stop(sourceId_);
		}

		return audioStream_.loadFromMemory(bufferPtr, bufferSize);
	}*/

	bool AudioStreamPlayer::loadFromFile(const char* filename)
	{
		if (state_ != PlayerState::Stopped) {
			audioStream_.stop(sourceId_);
		}

		return audioStream_.loadFromFile(filename);
	}

	void AudioStreamPlayer::play()
	{
		IAudioDevice& device = theServiceLocator().audioDevice();

		switch (state_) {
			case PlayerState::Initial:
			case PlayerState::Stopped: {
				const unsigned int source = device.registerPlayer(this);
				if (source == IAudioDevice::UnavailableSource) {
					LOGW("No more available audio sources for playing");
					break;
				}
				sourceId_ = source;

				// Streams looping is not handled at enqueued buffer level
				alSourcei(sourceId_, AL_LOOPING, AL_FALSE);

				alSourcef(sourceId_, AL_GAIN, gain_);
				alSourcef(sourceId_, AL_PITCH, pitch_);

				updateFilters();

				bool isSourceRelative = GetFlags(PlayerFlags::SourceRelative);
				bool isAs2D = GetFlags(PlayerFlags::As2D);
				Vector3f adjustedPos = getAdjustedPosition(device, position_, isSourceRelative, isAs2D);

				alSourcei(sourceId_, AL_SOURCE_RELATIVE, isSourceRelative || isAs2D ? AL_TRUE : AL_FALSE);
				alSource3f(sourceId_, AL_POSITION, adjustedPos.X, adjustedPos.Y, adjustedPos.Z);
				alSourcef(sourceId_, AL_REFERENCE_DISTANCE, IAudioDevice::ReferenceDistance);
				alSourcef(sourceId_, AL_MAX_DISTANCE, IAudioDevice::MaxDistance);

				alSourcePlay(sourceId_);
				state_ = PlayerState::Playing;
				break;
			}
			case PlayerState::Paused: {
				updateFilters();

				alSourcePlay(sourceId_);
				state_ = PlayerState::Playing;
				break;
			}
		}
	}

	void AudioStreamPlayer::pause()
	{
		switch (state_) {
			case PlayerState::Playing: {
				alSourcePause(sourceId_);
				state_ = PlayerState::Paused;
				break;
			}
		}
	}

	void AudioStreamPlayer::stop()
	{
		switch (state_) {
			case PlayerState::Playing:
			case PlayerState::Paused: {
				// Stop the source then unqueue every buffer
				audioStream_.stop(sourceId_);
				// Detach the buffer from source
				alSourcei(sourceId_, AL_BUFFER, 0);
#if defined(OPENAL_FILTERS_SUPPORTED)
				if (filterHandle_ != 0) {
					alSourcei(sourceId_, AL_DIRECT_FILTER, 0);
				}
#endif
				state_ = PlayerState::Stopped;
				break;
			}
		}

		IAudioDevice& device = theServiceLocator().audioDevice();
		device.unregisterPlayer(this);
	}

	void AudioStreamPlayer::setLooping(bool value)
	{
		IAudioPlayer::setLooping(value);

		audioStream_.setLooping(value);
	}

	void AudioStreamPlayer::updateState()
	{
		if (state_ == PlayerState::Playing) {
			const bool shouldStillPlay = audioStream_.enqueue(sourceId_, GetFlags(PlayerFlags::Looping));
			if (!shouldStillPlay) {
				// Detach the buffer from source
				alSourcei(sourceId_, AL_BUFFER, 0);
#if defined(OPENAL_FILTERS_SUPPORTED)
				if (filterHandle_ != 0) {
					alSourcei(sourceId_, AL_DIRECT_FILTER, 0);
				}
#endif
				state_ = PlayerState::Stopped;

				IAudioDevice& device = theServiceLocator().audioDevice();
				device.unregisterPlayer(this);
			}
		}
	}
}
