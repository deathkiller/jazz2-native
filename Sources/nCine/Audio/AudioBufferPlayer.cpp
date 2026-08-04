#include "AudioBufferPlayer.h"
#include "AudioBuffer.h"
#include "../ServiceLocator.h"

namespace nCine
{
	AudioBufferPlayer::AudioBufferPlayer()
		: IAudioPlayer(ObjectType::AudioBufferPlayer), _audioBuffer(nullptr)
	{
	}

	AudioBufferPlayer::AudioBufferPlayer(AudioBuffer* audioBuffer)
		: IAudioPlayer(ObjectType::AudioBufferPlayer), _audioBuffer(audioBuffer)
	{
	}

	AudioBufferPlayer::~AudioBufferPlayer()
	{
		stop();
	}

	std::uint32_t AudioBufferPlayer::bufferId() const
	{
		std::uint32_t bufferId = (_audioBuffer != nullptr ? _audioBuffer->bufferId() : 0U);
		return (_state != PlayerState::Initial && _state != PlayerState::Stopped ? bufferId : 0U);
	}

	std::int32_t AudioBufferPlayer::bytesPerSample() const
	{
		return (_audioBuffer != nullptr ? _audioBuffer->bytesPerSample() : 0);
	}

	std::int32_t AudioBufferPlayer::numChannels() const
	{
		return (_audioBuffer != nullptr ? _audioBuffer->numChannels() : 0);
	}

	std::int32_t AudioBufferPlayer::frequency() const
	{
		return (_audioBuffer != nullptr ? _audioBuffer->frequency() : 0);
	}

	std::int32_t AudioBufferPlayer::numSamples() const
	{
		return (_audioBuffer != nullptr ? _audioBuffer->numSamples() : 0UL);
	}

	float AudioBufferPlayer::duration() const
	{
		return (_audioBuffer != nullptr ? _audioBuffer->duration() : 0.0f);
	}

	std::int32_t AudioBufferPlayer::bufferSize() const
	{
		return (_audioBuffer != nullptr ? _audioBuffer->bufferSize() : 0);
	}

	void AudioBufferPlayer::setAudioBuffer(AudioBuffer* audioBuffer)
	{
		stop();
		_audioBuffer = audioBuffer;
	}

	void AudioBufferPlayer::play()
	{
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		switch (_state) {
			case PlayerState::Initial:
			case PlayerState::Stopped: {
				if (_audioBuffer == nullptr) {
					break;
				}

				const unsigned int source = device.registerPlayer(this);
				if (source == IAudioDevice::UnavailableSource) {
					if (device.isValid()) {
						LOGW("No more available audio sources for playing");
					}
					break;
				}
				_sourceId = source;

				device.setSourceBuffer(_sourceId, _audioBuffer->bufferId());
				// Setting source looping only if not streaming
				device.setSourceLooping(_sourceId, GetFlags(PlayerFlags::Looping));

				device.setSourceGain(_sourceId, _gain);
				device.setSourcePitch(_sourceId, _pitch);

				updateFilters();

				bool isSourceRelative = GetFlags(PlayerFlags::SourceRelative);
				bool isAs2D = GetFlags(PlayerFlags::As2D);

				device.setSourceRelative(_sourceId, isSourceRelative || isAs2D);
				setPositionInternal(getAdjustedPosition(device, _position, isSourceRelative, isAs2D));

				device.playSource(_sourceId);
				_state = PlayerState::Playing;
				break;
			}
			case PlayerState::Paused: {
				updateFilters();

				device.playSource(_sourceId);
				_state = PlayerState::Playing;
				break;
			}
		}
	}

	void AudioBufferPlayer::pause()
	{
		switch (_state) {
			case PlayerState::Playing: {
				theServiceLocator().GetAudioDevice().pauseSource(_sourceId);
				_state = PlayerState::Paused;
				break;
			}
		}
	}

	void AudioBufferPlayer::stop()
	{
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		switch (_state) {
			case PlayerState::Playing:
			case PlayerState::Paused: {
				device.stopSource(_sourceId);
				// Detach the buffer from source
				device.setSourceBuffer(_sourceId, 0);
				device.setSourceLowPass(_sourceId, 1.0f);
				_state = PlayerState::Stopped;
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
			if (_sourceId != IAudioDevice::UnavailableSource) {
				theServiceLocator().GetAudioDevice().setSourceLooping(_sourceId, value);
			}
		}
	}

	void AudioBufferPlayer::updateState()
	{
		if (_state == PlayerState::Playing) {
			IAudioDevice& device = theServiceLocator().GetAudioDevice();
			if (!device.isSourcePlaying(_sourceId)) {
				// Detach the buffer from source
				device.setSourceBuffer(_sourceId, 0);
				device.setSourceLowPass(_sourceId, 1.0f);
				_state = PlayerState::Stopped;

				device.unregisterPlayer(this);
			}
		}
	}
}
