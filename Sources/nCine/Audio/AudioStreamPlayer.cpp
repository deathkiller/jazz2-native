#include "AudioStreamPlayer.h"
#include "../ServiceLocator.h"

namespace nCine
{
	AudioStreamPlayer::AudioStreamPlayer()
		: IAudioPlayer(ObjectType::AudioStreamPlayer), audioStream_()
	{
	}

	AudioStreamPlayer::AudioStreamPlayer(StringView filename)
		: IAudioPlayer(ObjectType::AudioStreamPlayer), audioStream_(filename)
	{
	}

	AudioStreamPlayer::~AudioStreamPlayer()
	{
		stop();
	}

	bool AudioStreamPlayer::loadFromFile(const char* filename)
	{
		if (state_ != PlayerState::Stopped) {
			audioStream_.stop(sourceId_);
		}

		return audioStream_.loadFromFile(filename);
	}

	void AudioStreamPlayer::play()
	{
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		switch (state_) {
			case PlayerState::Initial:
			case PlayerState::Stopped: {
				const unsigned int source = device.registerPlayer(this);
				if (source == IAudioDevice::UnavailableSource) {
					if (device.isValid()) {
						LOGW("No more available audio sources for playing");
					}
					break;
				}
				sourceId_ = source;

				// Streams looping is not handled at enqueued buffer level
				device.setSourceLooping(sourceId_, false);

				device.setSourceGain(sourceId_, gain_);
				device.setSourcePitch(sourceId_, pitch_);

				updateFilters();

				bool isSourceRelative = GetFlags(PlayerFlags::SourceRelative);
				bool isAs2D = GetFlags(PlayerFlags::As2D);

				device.setSourceRelative(sourceId_, isSourceRelative || isAs2D);
				setPositionInternal(getAdjustedPosition(device, position_, isSourceRelative, isAs2D));

				state_ = PlayerState::Playing;

				// Fill the queue here instead of leaving the first refill to updateState(): the stream
				// starts a frame earlier that way, and the source is never left in the "playing with an
				// empty queue" state. OpenAL Soft treats that state as an immediate end, but ALdc walks
				// its buffer queue with an iterator that is not valid until something is queued and
				// asserts its way into a kernel panic instead. `enqueue()` starts the source itself once
				// it has queued the first buffer, which is the same path it uses to recover an underrun.
				if (!audioStream_.enqueue(sourceId_, GetFlags(PlayerFlags::Looping))) {
					// Nothing could be decoded at all, there is no stream to play
					state_ = PlayerState::Stopped;
					device.setSourceBuffer(sourceId_, 0);
					device.unregisterPlayer(this);
				}
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

	void AudioStreamPlayer::pause()
	{
		switch (state_) {
			case PlayerState::Playing: {
				theServiceLocator().GetAudioDevice().pauseSource(sourceId_);
				state_ = PlayerState::Paused;
				break;
			}
		}
	}

	void AudioStreamPlayer::stop()
	{
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		switch (state_) {
			case PlayerState::Playing:
			case PlayerState::Paused: {
				// Stop the source then unqueue every buffer
				audioStream_.stop(sourceId_);
				// Detach the buffer from source
				device.setSourceBuffer(sourceId_, 0);
				device.setSourceLowPass(sourceId_, 1.0f);
				state_ = PlayerState::Stopped;
				break;
			}
		}

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
			bool shouldStillPlay = audioStream_.enqueue(sourceId_, GetFlags(PlayerFlags::Looping));
			if (!shouldStillPlay) {
				IAudioDevice& device = theServiceLocator().GetAudioDevice();
				// Detach the buffer from source
				device.setSourceBuffer(sourceId_, 0);
				device.setSourceLowPass(sourceId_, 1.0f);
				state_ = PlayerState::Stopped;

				device.unregisterPlayer(this);
			}
		}
	}
}
