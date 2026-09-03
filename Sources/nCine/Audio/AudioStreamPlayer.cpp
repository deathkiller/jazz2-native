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
		stop();
	}

	bool AudioStreamPlayer::loadFromFile(const char* filename)
	{
		if (_state != PlayerState::Stopped) {
			_audioStream.stop(_sourceId);
		}

		return _audioStream.loadFromFile(filename);
	}

	void AudioStreamPlayer::play()
	{
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		switch (_state) {
			case PlayerState::Initial:
			case PlayerState::Stopped: {
				const unsigned int source = device.registerPlayer(this);
				if DEATH_UNLIKELY(source == IAudioDevice::UnavailableSource) {
					if (device.isValid()) {
						LOGW("No more available audio sources for playing");
					}
					break;
				}
				_sourceId = source;

				// Streams looping is not handled at enqueued buffer level
				device.setSourceLooping(_sourceId, false);

				device.setSourceGain(_sourceId, _gain);
				device.setSourcePitch(_sourceId, _pitch);

				updateFilters();

				bool isSourceRelative = GetFlags(PlayerFlags::SourceRelative);
				bool isAs2D = GetFlags(PlayerFlags::As2D);

				device.setSourceRelative(_sourceId, isSourceRelative || isAs2D);
				setPositionInternal(getAdjustedPosition(device, _position, isSourceRelative, isAs2D));

				_state = PlayerState::Playing;

				// Fill the queue here instead of leaving the first refill to updateState(): the stream
				// starts a frame earlier that way, and the source is never left in the "playing with an
				// empty queue" state. OpenAL Soft treats that state as an immediate end, but ALdc walks
				// its buffer queue with an iterator that is not valid until something is queued and
				// asserts its way into a kernel panic instead. `enqueue()` starts the source itself once
				// it has queued the first buffer, which is the same path it uses to recover an underrun.
				// Nothing is primed for a stream that is turned all the way down - that first chunk is a
				// decode like any other. The source is left registered but never started, which is safe
				// precisely because nothing is queued on it: the state the comment above warns about is
				// a source that has been told to play with an empty queue, and playSource() only ever
				// happens from inside enqueue(), after a buffer is on the queue.
				if (!isSilent() && !_audioStream.enqueue(_sourceId, GetFlags(PlayerFlags::Looping))) {
					// Nothing could be decoded at all, there is no stream to play
					_state = PlayerState::Stopped;
					device.setSourceBuffer(_sourceId, 0);
					device.unregisterPlayer(this);
				}
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

	void AudioStreamPlayer::pause()
	{
		switch (_state) {
			case PlayerState::Playing: {
				theServiceLocator().GetAudioDevice().pauseSource(_sourceId);
				_state = PlayerState::Paused;
				break;
			}
		}
	}

	void AudioStreamPlayer::stop()
	{
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		switch (_state) {
			case PlayerState::Playing:
			case PlayerState::Paused: {
				// Stop the source then unqueue every buffer
				_audioStream.stop(_sourceId);
				// Detach the buffer from source
				device.setSourceBuffer(_sourceId, 0);
				device.setSourceLowPass(_sourceId, 1.0f);
				_state = PlayerState::Stopped;
				break;
			}
		}

		device.unregisterPlayer(this);
	}

	void AudioStreamPlayer::setLooping(bool value)
	{
		IAudioPlayer::setLooping(value);

		_audioStream.setLooping(value);
	}

	void AudioStreamPlayer::updateState()
	{
		if (_state == PlayerState::Playing) {
			// Decoding is by far the most expensive thing a streamed player does and none of it is worth doing
			// for a stream turned all the way down. The player stays registered and stays in the Playing state,
			// so this resumes by itself the moment the gain comes back: what is already queued plays out,
			// the source runs dry, and the underrun path inside enqueue() starts it again.
			if DEATH_UNLIKELY(isSilent()) {
				return;
			}

			bool shouldStillPlay = _audioStream.enqueue(_sourceId, GetFlags(PlayerFlags::Looping));
			if (!shouldStillPlay) {
				IAudioDevice& device = theServiceLocator().GetAudioDevice();
				// Detach the buffer from source
				device.setSourceBuffer(_sourceId, 0);
				device.setSourceLowPass(_sourceId, 1.0f);
				_state = PlayerState::Stopped;

				device.unregisterPlayer(this);
			}
		}
	}
}
