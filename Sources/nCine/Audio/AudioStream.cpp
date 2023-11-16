#define NCINE_INCLUDE_OPENAL
#include "../CommonHeaders.h"

#include "AudioStream.h"
#include "IAudioLoader.h"
#include "IAudioReader.h"
#include "../ServiceLocator.h"

namespace nCine
{
	/*! Private constructor called only by `AudioStreamPlayer`. */
	AudioStream::AudioStream()
		: nextAvailableBufferIndex_(0), currentBufferId_(0), bytesPerSample_(0), numChannels_(0), isLooping_(false),
			frequency_(0), numSamples_(0), duration_(0.0f), buffersIds_(NumBuffers)
	{
		alGetError();
		alGenBuffers(NumBuffers, buffersIds_.data());
		const ALenum error = alGetError();
		ASSERT_MSG(error == AL_NO_ERROR, "alGenBuffers failed: 0x%x", error);
		memBuffer_ = std::make_unique<char[]>(BufferSize);
	}

	/*! Private constructor called only by `AudioStreamPlayer`. */
	AudioStream::AudioStream(const unsigned char* bufferPtr, unsigned long int bufferSize)
		: AudioStream()
	{
		const bool hasLoaded = loadFromMemory(bufferPtr, bufferSize);
		if (!hasLoaded) {
			LOGE("Audio buffer cannot be loaded");
		}
	}

	/*! Private constructor called only by `AudioStreamPlayer`. */
	AudioStream::AudioStream(const StringView& filename)
		: AudioStream()
	{
		const bool hasLoaded = loadFromFile(filename);
		if (!hasLoaded) {
			LOGE("Audio file \"%s\" cannot be loaded", filename.data());
		}
	}

	AudioStream::~AudioStream()
	{
		// Don't delete buffers if this is a moved out object
		if (buffersIds_.size() == NumBuffers) {
			alDeleteBuffers(NumBuffers, buffersIds_.data());
		}
	}

	AudioStream::AudioStream(AudioStream&&) = default;

	AudioStream& AudioStream::operator=(AudioStream&&) = default;

	unsigned long int AudioStream::numStreamSamples() const
	{
		if (numChannels_ * bytesPerSample_ > 0) {
			return BufferSize / (numChannels_ * bytesPerSample_);
		}
		return 0UL;
	}

	/*! \return A flag indicating whether the stream has been entirely decoded and played or not. */
	bool AudioStream::enqueue(unsigned int source, bool looping)
	{
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

			unsigned long bytes = audioReader_->read(memBuffer_.get(), BufferSize);

			// EOF reached
			if (bytes < BufferSize) {
				if (looping) {
					audioReader_->rewind();
					const unsigned long moreBytes = audioReader_->read(memBuffer_.get() + bytes, BufferSize - bytes);
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
	}

	void AudioStream::stop(unsigned int source)
	{
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
	}

	void AudioStream::setLooping(bool isLooping)
	{
		isLooping_ = isLooping;

		if (audioReader_ != nullptr) {
			audioReader_->setLooping(isLooping_);
		}
	}

	bool AudioStream::loadFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		std::unique_ptr<IAudioLoader> audioLoader = IAudioLoader::createFromMemory(bufferPtr, bufferSize);
		if (!audioLoader->hasLoaded()) {
			return false;
		}
		createReader(*audioLoader);
		return true;
	}

	bool AudioStream::loadFromFile(const StringView& filename)
	{
		std::unique_ptr<IAudioLoader> audioLoader = IAudioLoader::createFromFile(filename);
		if (!audioLoader->hasLoaded()) {
			return false;
		}
		createReader(*audioLoader);
		return true;
	}

	void AudioStream::createReader(IAudioLoader& audioLoader)
	{
		bytesPerSample_ = audioLoader.bytesPerSample();
		numChannels_ = audioLoader.numChannels();

		if (numChannels_ == 1) {
			format_ = (bytesPerSample_ == 2 ? AL_FORMAT_MONO16 : AL_FORMAT_MONO8);
		} else if (numChannels_ == 2) {
			format_ = (bytesPerSample_ == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_STEREO8);
		} else {
			bytesPerSample_ = 0;
			numChannels_ = 0;
			RETURN_MSG("Audio stream with %i channels is not supported", numChannels_);
		}

		frequency_ = audioLoader.frequency();
		numSamples_ = audioLoader.numSamples();
		duration_ = (numSamples_ == UINT32_MAX ? -1.0f : float(numSamples_) / frequency_);

		audioReader_ = audioLoader.createReader();
		audioReader_->setLooping(isLooping_);
	}
}
