#if defined(WITH_AUDIO)
#	define NCINE_INCLUDE_OPENAL
#	include "../CommonHeaders.h"
#endif

#include "AudioStream.h"
#include "IAudioLoader.h"
#include "IAudioReader.h"
#include "../ServiceLocator.h"

#include <Containers/String.h>

namespace nCine
{
	/*! Private constructor called only by `AudioStreamPlayer`. */
	AudioStream::AudioStream()
		: nextAvailableBufferIndex_(0), currentBufferId_(0), bytesPerSample_(0), numChannels_(0), isLooping_(false),
			frequency_(0), numSamples_(0), duration_(0.0f), buffersIds_(NumBuffers)
	{
#if defined(WITH_AUDIO)
		alGetError();
		alGenBuffers(NumBuffers, buffersIds_.data());
		const ALenum error = alGetError();
		if DEATH_UNLIKELY(error != AL_NO_ERROR) {
			LOGW("alGenBuffers() failed with error 0x{:x}", error);
		}
		memBuffer_ = std::make_unique<char[]>(BufferSize);
#endif
	}

	/*! Private constructor called only by `AudioStreamPlayer`. */
	AudioStream::AudioStream(StringView filename)
		: AudioStream()
	{
#if defined(WITH_AUDIO)
		const bool hasLoaded = loadFromFile(filename);
		if (!hasLoaded) {
			LOGE("Audio file \"{}\" cannot be loaded", filename);
		}
#endif
	}

	AudioStream::~AudioStream()
	{
#if defined(WITH_AUDIO)
		// Don't delete buffers if this is a moved out object
		if (buffersIds_.size() == NumBuffers) {
			alDeleteBuffers(NumBuffers, buffersIds_.data());
		}
#endif
	}

	AudioStream::AudioStream(AudioStream&&) = default;
	AudioStream& AudioStream::operator=(AudioStream&&) = default;

	std::int32_t AudioStream::numStreamSamples() const
	{
#if defined(WITH_AUDIO)
		if (numChannels_ * bytesPerSample_ > 0) {
			return BufferSize / (numChannels_ * bytesPerSample_);
		}
#endif
		return 0UL;
	}

	/*! \return A flag indicating whether the stream has been entirely decoded and played or not. */
	bool AudioStream::enqueue(std::uint32_t source, bool looping)
	{
#if defined(WITH_AUDIO)
		if (audioReader_ == nullptr) {
			return false;
		}
		
		// Set to false when the queue is empty and there is no more data to decode
		bool shouldKeepPlaying = true;

		ALint numProcessedBuffers;
		alGetSourcei(source, AL_BUFFERS_PROCESSED, &numProcessedBuffers);

		// Unqueueing
		while (numProcessedBuffers > 0) {
			ALuint unqueuedAlBuffer;
			alSourceUnqueueBuffers(source, 1, &unqueuedAlBuffer);
			nextAvailableBufferIndex_--;
			buffersIds_[nextAvailableBufferIndex_] = unqueuedAlBuffer;
			numProcessedBuffers--;
		}

		// Queueing
		if (nextAvailableBufferIndex_ < NumBuffers) {
			currentBufferId_ = buffersIds_[nextAvailableBufferIndex_];

			std::int32_t bytes = audioReader_->read(memBuffer_.get(), BufferSize);

			// EOF reached
			if (bytes < BufferSize) {
				if (looping) {
					audioReader_->rewind();
					std::int32_t moreBytes = audioReader_->read(memBuffer_.get() + bytes, BufferSize - bytes);
					bytes += moreBytes;
				}
			}

			// If it is still decoding data then enqueue
			if (bytes > 0) {
				// On iOS `alBufferDataStatic()` could be used instead
				alBufferData(currentBufferId_, format_, memBuffer_.get(), bytes, frequency_);
				alSourceQueueBuffers(source, 1, &currentBufferId_);
				nextAvailableBufferIndex_++;
			}
			// If there is no more data left to decode and the queue is empty
			else if (nextAvailableBufferIndex_ == 0) {
				shouldKeepPlaying = false;
				stop(source);
			}
		}

		ALenum state;
		alGetSourcei(source, AL_SOURCE_STATE, &state);

		// Handle buffer underrun case
		if (state != AL_PLAYING) {
			ALint numQueuedBuffers = 0;
			alGetSourcei(source, AL_BUFFERS_QUEUED, &numQueuedBuffers);
			if (numQueuedBuffers > 0) {
				// Need to restart play
				alSourcePlay(source);
			}
		}

		return shouldKeepPlaying;
#else
		return false;
#endif
	}

	void AudioStream::stop(std::uint32_t source)
	{
#if defined(WITH_AUDIO)
		// In order to unqueue all the buffers, the source must be stopped first
		alSourceStop(source);

		ALint numProcessedBuffers;
		alGetSourcei(source, AL_BUFFERS_PROCESSED, &numProcessedBuffers);

		// Unqueueing
		while (numProcessedBuffers > 0) {
			ALuint unqueuedAlBuffer;
			alSourceUnqueueBuffers(source, 1, &unqueuedAlBuffer);
			nextAvailableBufferIndex_--;
			buffersIds_[nextAvailableBufferIndex_] = unqueuedAlBuffer;
			numProcessedBuffers--;
		}

		audioReader_->rewind();
		currentBufferId_ = 0;
#endif
	}

	void AudioStream::setLooping(bool value)
	{
		isLooping_ = value;

#if defined(WITH_AUDIO)
		if (audioReader_ != nullptr) {
			audioReader_->setLooping(value);
		}
#endif
	}

	bool AudioStream::loadFromFile(StringView filename)
	{
#if defined(WITH_AUDIO)
		std::unique_ptr<IAudioLoader> audioLoader = IAudioLoader::createFromFile(filename);
		if (audioLoader->hasLoaded()) {
			createReader(*audioLoader);
			return true;
		}	
#endif
		return false;
	}

	void AudioStream::createReader(IAudioLoader& audioLoader)
	{
#if defined(WITH_AUDIO)
		bytesPerSample_ = audioLoader.bytesPerSample();
		numChannels_ = audioLoader.numChannels();

		if (numChannels_ == 1) {
			format_ = (bytesPerSample_ == 2 ? AL_FORMAT_MONO16 : AL_FORMAT_MONO8);
		} else if (numChannels_ == 2) {
			format_ = (bytesPerSample_ == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_STEREO8);
		} else {
			bytesPerSample_ = 0;
			numChannels_ = 0;
			LOGE("Audio stream with {} channels is not supported", numChannels_);
			return;
		}

		frequency_ = audioLoader.frequency();
		numSamples_ = audioLoader.numSamples();
		duration_ = (numSamples_ == UINT32_MAX ? -1.0f : float(numSamples_) / frequency_);

		audioReader_ = audioLoader.createReader();
		audioReader_->setLooping(isLooping_);
#endif
	}
}
