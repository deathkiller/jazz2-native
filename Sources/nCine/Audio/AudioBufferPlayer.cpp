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
		Stop();
	}

	std::uint32_t AudioBufferPlayer::GetBufferId() const
	{
		std::uint32_t bufferId = (_audioBuffer != nullptr ? _audioBuffer->GetBufferId() : 0U);
		return (_state != PlayerState::Initial && _state != PlayerState::Stopped ? bufferId : 0U);
	}

	std::int32_t AudioBufferPlayer::GetBytesPerSample() const
	{
		return (_audioBuffer != nullptr ? _audioBuffer->GetBytesPerSample() : 0);
	}

	std::int32_t AudioBufferPlayer::GetChannelCount() const
	{
		return (_audioBuffer != nullptr ? _audioBuffer->GetChannelCount() : 0);
	}

	std::int32_t AudioBufferPlayer::GetFrequency() const
	{
		return (_audioBuffer != nullptr ? _audioBuffer->GetFrequency() : 0);
	}

	std::int32_t AudioBufferPlayer::GetSampleCount() const
	{
		return (_audioBuffer != nullptr ? _audioBuffer->GetSampleCount() : 0UL);
	}

	float AudioBufferPlayer::GetDuration() const
	{
		return (_audioBuffer != nullptr ? _audioBuffer->GetDuration() : 0.0f);
	}

	std::int32_t AudioBufferPlayer::GetBufferSize() const
	{
		return (_audioBuffer != nullptr ? _audioBuffer->GetBufferSize() : 0);
	}

	void AudioBufferPlayer::SetAudioBuffer(AudioBuffer* audioBuffer)
	{
		Stop();
		_audioBuffer = audioBuffer;
	}

	void AudioBufferPlayer::Play()
	{
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		switch (_state) {
			case PlayerState::Initial:
			case PlayerState::Stopped: {
				if DEATH_UNLIKELY(_audioBuffer == nullptr) {
					break;
				}
				// Don't play a silent sound at all, callers already handle a stopped player
				if DEATH_UNLIKELY(IsSilent()) {
					break;
				}

				// A refusal is reported by the device itself, which can say what is holding the sources
				// and can keep a caller that retries every frame from flooding the log
				const std::uint32_t source = device.RegisterPlayer(this);
				if DEATH_UNLIKELY(source == IAudioDevice::UnavailableSource) {
					break;
				}
				_sourceId = source;

				device.SetSourceBuffer(_sourceId, _audioBuffer->GetBufferId());
				// Setting source looping only if not streaming
				device.SetSourceLooping(_sourceId, GetFlags(PlayerFlags::Looping));

				device.SetSourceGain(_sourceId, _gain);
				device.SetSourcePitch(_sourceId, _pitch);

				UpdateFilters();

				bool isSourceRelative = GetFlags(PlayerFlags::SourceRelative);
				bool isAs2D = GetFlags(PlayerFlags::As2D);

				device.SetSourceRelative(_sourceId, isSourceRelative || isAs2D);
				SetPositionInternal(GetAdjustedPosition(device, _position, isSourceRelative, isAs2D));

				device.PlaySource(_sourceId);
				_state = PlayerState::Playing;
				break;
			}
			case PlayerState::Paused: {
				UpdateFilters();

				device.PlaySource(_sourceId);
				_state = PlayerState::Playing;
				break;
			}
		}
	}

	void AudioBufferPlayer::Pause()
	{
		switch (_state) {
			case PlayerState::Playing: {
				theServiceLocator().GetAudioDevice().PauseSource(_sourceId);
				_state = PlayerState::Paused;
				break;
			}
		}
	}

	void AudioBufferPlayer::Stop()
	{
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		switch (_state) {
			case PlayerState::Playing:
			case PlayerState::Paused: {
				device.StopSource(_sourceId);
				// Detach the buffer from source
				device.SetSourceBuffer(_sourceId, 0);
				device.SetSourceLowPass(_sourceId, 1.0f);
				_state = PlayerState::Stopped;
				break;
			}
		}

		device.UnregisterPlayer(this);
	}

	void AudioBufferPlayer::SetLooping(bool value)
	{
		if (IsLooping() != value) {
			IAudioPlayer::SetLooping(value);
			// Applying the change immediately, so the per-frame update doesn't have to re-set it
			if (_sourceId != IAudioDevice::UnavailableSource) {
				theServiceLocator().GetAudioDevice().SetSourceLooping(_sourceId, value);
			}
		}
	}

	void AudioBufferPlayer::UpdateState()
	{
		if (_state == PlayerState::Playing) {
			IAudioDevice& device = theServiceLocator().GetAudioDevice();
			if (!device.IsSourcePlaying(_sourceId)) {
				// Detach the buffer from source
				device.SetSourceBuffer(_sourceId, 0);
				device.SetSourceLowPass(_sourceId, 1.0f);
				_state = PlayerState::Stopped;

				device.UnregisterPlayer(this);
			}
		}
	}
}
