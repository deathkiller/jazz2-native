#include "AudioBufferPlayer.h"
#include "AudioBuffer.h"
#include "../ServiceLocator.h"

#define NCINE_INCLUDE_OPENAL
#include "../CommonHeaders.h"

namespace nCine
{
	AudioBufferPlayer::AudioBufferPlayer()
		: IAudioPlayer(ObjectType::AudioBufferPlayer), audioBuffer_(nullptr)
	{
	}

	AudioBufferPlayer::AudioBufferPlayer(AudioBuffer* audioBuffer)
		: IAudioPlayer(ObjectType::AudioBufferPlayer), audioBuffer_(audioBuffer)
	{
	}

	AudioBufferPlayer::~AudioBufferPlayer()
	{
		stop();
	}

	unsigned int AudioBufferPlayer::bufferId() const
	{
		const unsigned int bufferId = (audioBuffer_ != nullptr ? audioBuffer_->bufferId() : 0U);
		return (state_ != PlayerState::Initial && state_ != PlayerState::Stopped ? bufferId : 0U);
	}

	int AudioBufferPlayer::bytesPerSample() const
	{
		return (audioBuffer_ != nullptr ? audioBuffer_->bytesPerSample() : 0);
	}

	int AudioBufferPlayer::numChannels() const
	{
		return (audioBuffer_ != nullptr ? audioBuffer_->numChannels() : 0);
	}

	int AudioBufferPlayer::frequency() const
	{
		return (audioBuffer_ != nullptr ? audioBuffer_->frequency() : 0);
	}

	unsigned long int AudioBufferPlayer::numSamples() const
	{
		return (audioBuffer_ != nullptr ? audioBuffer_->numSamples() : 0UL);
	}

	float AudioBufferPlayer::duration() const
	{
		return (audioBuffer_ != nullptr ? audioBuffer_->duration() : 0.0f);
	}

	unsigned long int AudioBufferPlayer::bufferSize() const
	{
		return (audioBuffer_ != nullptr ? audioBuffer_->bufferSize() : 0UL);
	}

	void AudioBufferPlayer::setAudioBuffer(AudioBuffer* audioBuffer)
	{
		stop();
		audioBuffer_ = audioBuffer;
	}

	void AudioBufferPlayer::play()
	{
		IAudioDevice& device = theServiceLocator().audioDevice();

		switch (state_) {
			case PlayerState::Initial:
			case PlayerState::Stopped: {
				if (audioBuffer_ == nullptr) {
					break;
				}

				const unsigned int source = device.registerPlayer(this);
				if (source == IAudioDevice::UnavailableSource) {
					LOGW("No more available audio sources for playing");
					break;
				}
				sourceId_ = source;

				alSourcei(sourceId_, AL_BUFFER, audioBuffer_->bufferId());
				// Setting OpenAL source looping only if not streaming
				alSourcei(sourceId_, AL_LOOPING, GetFlags(PlayerFlags::Looping));

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

	void AudioBufferPlayer::pause()
	{
		switch (state_) {
			case PlayerState::Playing: {
				alSourcePause(sourceId_);
				state_ = PlayerState::Paused;
				break;
			}
		}
	}

	void AudioBufferPlayer::stop()
	{
		switch (state_) {
			case PlayerState::Playing:
			case PlayerState::Paused: {
				alSourceStop(sourceId_);
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

	void AudioBufferPlayer::updateState()
	{
		if (state_ == PlayerState::Playing) {
			ALenum alState;
			alGetSourcei(sourceId_, AL_SOURCE_STATE, &alState);

			if (alState != AL_PLAYING) {
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
			} else {
				alSourcei(sourceId_, AL_LOOPING, GetFlags(PlayerFlags::Looping));
			}
		}
	}
}
