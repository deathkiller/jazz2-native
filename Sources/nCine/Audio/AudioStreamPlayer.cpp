#include "AudioStreamPlayer.h"
#include "../ServiceLocator.h"

namespace nCine
{
	AudioStreamPlayer::AudioStreamPlayer()
		: IAudioPlayer(ObjectType::AudioStreamPlayer), _audioStream()
	{
	}

	AudioStreamPlayer::AudioStreamPlayer(StringView filename)
		: IAudioPlayer(ObjectType::AudioStreamPlayer), _audioStream(filename)
	{
	}

	AudioStreamPlayer::~AudioStreamPlayer()
	{
		Stop();
	}

	bool AudioStreamPlayer::LoadFromFile(const char* filename)
	{
		if (_state != PlayerState::Stopped) {
			_audioStream.Stop(_sourceId);
		}

		return _audioStream.LoadFromFile(filename);
	}

	void AudioStreamPlayer::Play()
	{
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		switch (_state) {
			case PlayerState::Initial:
			case PlayerState::Stopped: {
				// A refusal is reported by the device itself, see AudioBufferPlayer::Play()
				const std::uint32_t source = device.RegisterPlayer(this);
				if DEATH_UNLIKELY(source == IAudioDevice::UnavailableSource) {
					break;
				}
				_sourceId = source;

				// Streams looping is not handled at enqueued buffer level
				device.SetSourceLooping(_sourceId, false);

				device.SetSourceGain(_sourceId, _gain);
				device.SetSourcePitch(_sourceId, _pitch);

				UpdateFilters();

				bool isSourceRelative = GetFlags(PlayerFlags::SourceRelative);
				bool isAs2D = GetFlags(PlayerFlags::As2D);

				device.SetSourceRelative(_sourceId, isSourceRelative || isAs2D);
				SetPositionInternal(GetAdjustedPosition(device, _position, isSourceRelative, isAs2D));

				_state = PlayerState::Playing;

				// Prime the queue now, so the source never plays with an empty queue (ALdc panics on that).
				// A silent stream isn't primed, Enqueue() starts the source only after queueing a buffer.
				if (!IsSilent() && !_audioStream.Enqueue(_sourceId, GetFlags(PlayerFlags::Looping))) {
					// Nothing could be decoded at all, there is no stream to play
					_state = PlayerState::Stopped;
					device.SetSourceBuffer(_sourceId, 0);
					device.UnregisterPlayer(this);
				}
				break;
			}
			case PlayerState::Paused: {
				UpdateFilters();

				_state = PlayerState::Playing;
#if !defined(NCINE_HAS_NATIVE_AUDIO)
				// SetModulePosition() dropped the queue while paused, Enqueue() fills it again and starts the source
				if (_audioStream._nextAvailableBufferIndex == 0) {
					if (!IsSilent() && !_audioStream.Enqueue(_sourceId, GetFlags(PlayerFlags::Looping))) {
						_state = PlayerState::Stopped;
						device.SetSourceBuffer(_sourceId, 0);
						device.SetSourceLowPass(_sourceId, 1.0f);
						device.UnregisterPlayer(this);
					}
					break;
				}
#endif
				device.PlaySource(_sourceId);
				break;
			}
		}
	}

	void AudioStreamPlayer::Pause()
	{
		switch (_state) {
			case PlayerState::Playing: {
				theServiceLocator().GetAudioDevice().PauseSource(_sourceId);
				_state = PlayerState::Paused;
				break;
			}
		}
	}

	void AudioStreamPlayer::Stop()
	{
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		switch (_state) {
			case PlayerState::Playing:
			case PlayerState::Paused: {
				// Stop the source then unqueue every buffer
				_audioStream.Stop(_sourceId);
				// Detach the buffer from source
				device.SetSourceBuffer(_sourceId, 0);
				device.SetSourceLowPass(_sourceId, 1.0f);
				_state = PlayerState::Stopped;
				break;
			}
		}

		device.UnregisterPlayer(this);
	}

	void AudioStreamPlayer::SetLooping(bool value)
	{
		IAudioPlayer::SetLooping(value);

		_audioStream.SetLooping(value);
	}

	bool AudioStreamPlayer::SetModulePosition(std::int32_t order, std::int32_t row)
	{
		// A silent stream isn't decoded, there's nothing to restart
		bool restart = (_state == PlayerState::Playing && !IsSilent());
		return _audioStream.SetModulePosition(_sourceId, order, row, restart, GetFlags(PlayerFlags::Looping));
	}

	void AudioStreamPlayer::UpdateState()
	{
		if (_state == PlayerState::Playing) {
			// Don't decode a silent stream, it stays Playing and Enqueue() restarts it once the gain returns
			if DEATH_UNLIKELY(IsSilent()) {
				return;
			}

			bool shouldStillPlay = _audioStream.Enqueue(_sourceId, GetFlags(PlayerFlags::Looping));
			if (!shouldStillPlay) {
				IAudioDevice& device = theServiceLocator().GetAudioDevice();
				// Detach the buffer from source
				device.SetSourceBuffer(_sourceId, 0);
				device.SetSourceLowPass(_sourceId, 1.0f);
				_state = PlayerState::Stopped;

				device.UnregisterPlayer(this);
			}
		}
	}
}
