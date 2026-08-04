#include "AudioBufferPlayer.h"
#include "AudioBuffer.h"
#include "../ServiceLocator.h"

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

	std::uint32_t AudioBufferPlayer::bufferId() const
	{
		std::uint32_t bufferId = (audioBuffer_ != nullptr ? audioBuffer_->bufferId() : 0U);
		return (state_ != PlayerState::Initial && state_ != PlayerState::Stopped ? bufferId : 0U);
	}

	std::int32_t AudioBufferPlayer::bytesPerSample() const
	{
		return (audioBuffer_ != nullptr ? audioBuffer_->bytesPerSample() : 0);
	}

	std::int32_t AudioBufferPlayer::numChannels() const
	{
		return (audioBuffer_ != nullptr ? audioBuffer_->numChannels() : 0);
	}

	std::int32_t AudioBufferPlayer::frequency() const
	{
		return (audioBuffer_ != nullptr ? audioBuffer_->frequency() : 0);
	}

	std::int32_t AudioBufferPlayer::numSamples() const
	{
		return (audioBuffer_ != nullptr ? audioBuffer_->numSamples() : 0UL);
	}

	float AudioBufferPlayer::duration() const
	{
		return (audioBuffer_ != nullptr ? audioBuffer_->duration() : 0.0f);
	}

	std::int32_t AudioBufferPlayer::bufferSize() const
	{
		return (audioBuffer_ != nullptr ? audioBuffer_->bufferSize() : 0);
	}

	void AudioBufferPlayer::setAudioBuffer(AudioBuffer* audioBuffer)
	{
		stop();
		audioBuffer_ = audioBuffer;
	}

	void AudioBufferPlayer::play()
	{
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		switch (state_) {
			case PlayerState::Initial:
			case PlayerState::Stopped: {
				if (audioBuffer_ == nullptr) {
					break;
				}

				const unsigned int source = device.registerPlayer(this);
				if (source == IAudioDevice::UnavailableSource) {
					if (device.isValid()) {
						LOGW("No more available audio sources for playing");
					}
					break;
				}
				sourceId_ = source;

				device.setSourceBuffer(sourceId_, audioBuffer_->bufferId());
				// Setting source looping only if not streaming
				device.setSourceLooping(sourceId_, GetFlags(PlayerFlags::Looping));

				device.setSourceGain(sourceId_, gain_);
				device.setSourcePitch(sourceId_, pitch_);

				updateFilters();

				bool isSourceRelative = GetFlags(PlayerFlags::SourceRelative);
				bool isAs2D = GetFlags(PlayerFlags::As2D);

				device.setSourceRelative(sourceId_, isSourceRelative || isAs2D);
				setPositionInternal(getAdjustedPosition(device, position_, isSourceRelative, isAs2D));

				device.playSource(sourceId_);
				state_ = PlayerState::Playing;
				break;
			}
			case PlayerState::Paused: {
				updateFilters();

				device.playSource(sourceId_);
				state_ = PlayerState::Playing;
				break;
			}
		}
	}

	void AudioBufferPlayer::pause()
	{
		switch (state_) {
			case PlayerState::Playing: {
				theServiceLocator().GetAudioDevice().pauseSource(sourceId_);
				state_ = PlayerState::Paused;
				break;
			}
		}
	}

	void AudioBufferPlayer::stop()
	{
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		switch (state_) {
			case PlayerState::Playing:
			case PlayerState::Paused: {
				device.stopSource(sourceId_);
				// Detach the buffer from source
				device.setSourceBuffer(sourceId_, 0);
				device.setSourceLowPass(sourceId_, 1.0f);
				state_ = PlayerState::Stopped;
				break;
			}
		}

		device.unregisterPlayer(this);
	}

	void AudioBufferPlayer::setLooping(bool value)
	{
		if (isLooping() != value) {
			IAudioPlayer::setLooping(value);
			// Applying the change immediately, so the per-frame update doesn't have to re-set it
			if (sourceId_ != IAudioDevice::UnavailableSource) {
				theServiceLocator().GetAudioDevice().setSourceLooping(sourceId_, value);
			}
		}
	}

	void AudioBufferPlayer::updateState()
	{
		if (state_ == PlayerState::Playing) {
			IAudioDevice& device = theServiceLocator().GetAudioDevice();
			if (!device.isSourcePlaying(sourceId_)) {
				// Detach the buffer from source
				device.setSourceBuffer(sourceId_, 0);
				device.setSourceLowPass(sourceId_, 1.0f);
				state_ = PlayerState::Stopped;

				device.unregisterPlayer(this);
			}
		}
	}
}
