#include "AudioBufferPlayer.h"
#include "AudioBuffer.h"
#include "../../Common.h"
#include "../ServiceLocator.h"

#define NCINE_INCLUDE_OPENAL
#include "../CommonHeaders.h"

namespace nCine
{
	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	AudioBufferPlayer::AudioBufferPlayer()
		: IAudioPlayer(ObjectType::AUDIOBUFFER_PLAYER), audioBuffer_(nullptr), filterHandle_(0)
	{
	}

	AudioBufferPlayer::AudioBufferPlayer(AudioBuffer* audioBuffer)
		: IAudioPlayer(ObjectType::AUDIOBUFFER_PLAYER), audioBuffer_(audioBuffer), filterHandle_(0)
	{
		//if (audioBuffer)
		//	setName(audioBuffer->name());
	}

	AudioBufferPlayer::~AudioBufferPlayer()
	{
		stop();

#if !defined(__EMSCRIPTEN__)
		if (filterHandle_ != 0) {
			alDeleteFilters(1, &filterHandle_);
			filterHandle_ = 0;
		}
#endif

		// Force unregister to allow to destroy this player immediately
		IAudioDevice& device = theServiceLocator().audioDevice();
		device.unregisterPlayer(this);
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	unsigned int AudioBufferPlayer::bufferId() const
	{
		const unsigned int bufferId = audioBuffer_ ? audioBuffer_->bufferId() : 0U;
		return (state_ != PlayerState::Initial && state_ != PlayerState::Stopped) ? bufferId : 0U;
	}

	int AudioBufferPlayer::bytesPerSample() const
	{
		return (audioBuffer_ ? audioBuffer_->bytesPerSample() : 0);
	}

	int AudioBufferPlayer::numChannels() const
	{
		return (audioBuffer_ ? audioBuffer_->numChannels() : 0);
	}

	int AudioBufferPlayer::frequency() const
	{
		return (audioBuffer_ ? audioBuffer_->frequency() : 0);
	}

	unsigned long int AudioBufferPlayer::numSamples() const
	{
		return (audioBuffer_ ? audioBuffer_->numSamples() : 0UL);
	}

	float AudioBufferPlayer::duration() const
	{
		return (audioBuffer_ ? audioBuffer_->duration() : 0.0f);
	}

	unsigned long int AudioBufferPlayer::bufferSize() const
	{
		return (audioBuffer_ ? audioBuffer_->bufferSize() : 0UL);
	}

	void AudioBufferPlayer::setAudioBuffer(AudioBuffer* audioBuffer)
	{
		stop();
		audioBuffer_ = audioBuffer;
	}

	void AudioBufferPlayer::play()
	{
		IAudioDevice& device = theServiceLocator().audioDevice();
		const bool canRegisterPlayer = (device.numPlayers() < device.maxNumPlayers());

		switch (state_) {
			case PlayerState::Initial:
			case PlayerState::Stopped:
			{
				if (audioBuffer_ == nullptr || canRegisterPlayer == false)
					break;

				const unsigned int source = device.nextAvailableSource();
				if (source == IAudioDevice::UnavailableSource) {
					LOGW("No more available audio sources for playing");
					return;
				}
				sourceId_ = source;

				alSourcei(sourceId_, AL_BUFFER, audioBuffer_->bufferId());
				// Setting OpenAL source looping only if not streaming
				alSourcei(sourceId_, AL_LOOPING, isLooping_);

				alSourcef(sourceId_, AL_GAIN, gain_);
				alSourcef(sourceId_, AL_PITCH, pitch_);

#if !defined(__EMSCRIPTEN__)
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
				}
#endif

				alSourcefv(sourceId_, AL_POSITION, position_.Data());

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

				alSourcePlay(sourceId_);
				state_ = PlayerState::Playing;

				device.registerPlayer(this);
				break;
			}
		}
	}

	void AudioBufferPlayer::pause()
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

	void AudioBufferPlayer::stop()
	{
		switch (state_) {
			case PlayerState::Initial:
			case PlayerState::Stopped:
				break;
			case PlayerState::Playing:
			case PlayerState::Paused:
			{
				alSourceStop(sourceId_);
				// Detach the buffer from source
				alSourcei(sourceId_, AL_BUFFER, 0);
#if !defined(__EMSCRIPTEN__)
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

	void AudioBufferPlayer::updateState()
	{
		if (state_ == PlayerState::Playing) {
			ALenum alState;
			alGetSourcei(sourceId_, AL_SOURCE_STATE, &alState);

			if (alState != AL_PLAYING) {
				// Detach the buffer from source
				alSourcei(sourceId_, AL_BUFFER, 0);
#if !defined(__EMSCRIPTEN__)
				if (filterHandle_ != 0) {
					alSourcei(sourceId_, AL_DIRECT_FILTER, 0);
				}
#endif
				sourceId_ = 0;
				state_ = PlayerState::Stopped;
			} else {
				alSourcei(sourceId_, AL_LOOPING, isLooping_);
			}
		}
	}

}
