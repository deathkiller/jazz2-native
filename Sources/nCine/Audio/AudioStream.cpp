#if defined(WITH_AUDIO)
#	define NCINE_INCLUDE_OPENAL
#	include "../CommonHeaders.h"
#endif

#include "AudioStream.h"
#include "IAudioLoader.h"
#include "IAudioReader.h"
#if defined(WITH_AUDIO)
#	include "ALDebug.h"
#endif
#include "../ServiceLocator.h"

#include <Containers/String.h>

namespace nCine
{
	// Private constructor called only by AudioStreamPlayer
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
		decodeRequest_ = std::make_shared<StreamDecodeRequest>();
		decodeRequest_->buffer = std::make_unique<char[]>(BufferSize);
		decodeRequest_->bufferSize = BufferSize;
#endif
	}

	// Private constructor called only by AudioStreamPlayer
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
			AL_LOG_ERRORS();
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

	// Returns false once the stream has been entirely decoded and played
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

		// Unqueueing all processed buffers with a single call
		if (numProcessedBuffers > 0) {
			ALuint unqueuedAlBuffers[NumBuffers];
			alSourceUnqueueBuffers(source, numProcessedBuffers, unqueuedAlBuffers);
			for (ALint i = 0; i < numProcessedBuffers; i++) {
				nextAvailableBufferIndex_--;
				buffersIds_[nextAvailableBufferIndex_] = unqueuedAlBuffers[i];
			}
		}

		IAudioDevice& device = theServiceLocator().GetAudioDevice();
		bool reachedEndOfData = false;

		// Queueing until all available buffers are filled, so the queue is primed
		// in a single call and short frame hitches don't cause an underrun
		while (nextAvailableBufferIndex_ < NumBuffers) {
			StreamDecodeRequest::State requestState = decodeRequest_->state.load(std::memory_order_acquire);
			if (requestState == StreamDecodeRequest::State::Pending) {
				if (nextAvailableBufferIndex_ > 0) {
					// The next chunk is still being decoded, but there is queued data left to play, try again next frame
					break;
				}
				// The queue is empty (stream start or underrun after a long hitch), the data is needed right now
				device.drainStreamDecode(decodeRequest_);
				requestState = decodeRequest_->state.load(std::memory_order_acquire);
			}
			if (requestState != StreamDecodeRequest::State::Ready) {
				// Decode synchronously - the first fill after a start, or no decoding thread is available
				decodeRequest_->looping = looping;
				decodeRequest_->Execute();
			}

			const std::int32_t bytes = decodeRequest_->bytesRead;
			decodeRequest_->state.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);

			// If it is still decoding data then enqueue
			if (bytes > 0) {
				currentBufferId_ = buffersIds_[nextAvailableBufferIndex_];
				// On iOS `alBufferDataStatic()` could be used instead
				alBufferData(currentBufferId_, format_, decodeRequest_->buffer.get(), bytes, frequency_);
				alSourceQueueBuffers(source, 1, &currentBufferId_);
				nextAvailableBufferIndex_++;
			} else {
				reachedEndOfData = true;
				// If there is no more data left to decode and the queue is empty
				if (nextAvailableBufferIndex_ == 0) {
					shouldKeepPlaying = false;
					stop(source);
				}
				break;
			}
		}

		// Decode the next chunk ahead of time on the decoding thread while the queued buffers play
		if (shouldKeepPlaying && !reachedEndOfData &&
			decodeRequest_->state.load(std::memory_order_relaxed) == StreamDecodeRequest::State::Idle) {
			decodeRequest_->looping = looping;
			decodeRequest_->state.store(StreamDecodeRequest::State::Pending, std::memory_order_relaxed);
			if (!device.submitStreamDecode(decodeRequest_)) {
				// No decoding thread is available, the next chunk will be decoded synchronously instead
				decodeRequest_->state.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
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
		AL_LOG_ERRORS();

		return shouldKeepPlaying;
#else
		return false;
#endif
	}

	void AudioStream::stop(std::uint32_t source)
	{
#if defined(WITH_AUDIO)
		// The reader can't be rewound while the decoding thread is using it,
		// any chunk decoded ahead of time is stale after the rewind anyway
		if (decodeRequest_ != nullptr) {
			theServiceLocator().GetAudioDevice().drainStreamDecode(decodeRequest_);
			decodeRequest_->state.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
		}

		// In order to unqueue all the buffers, the source must be stopped first
		alSourceStop(source);

		ALint numProcessedBuffers;
		alGetSourcei(source, AL_BUFFERS_PROCESSED, &numProcessedBuffers);

		// Unqueueing all processed buffers with a single call
		if (numProcessedBuffers > 0) {
			ALuint unqueuedAlBuffers[NumBuffers];
			alSourceUnqueueBuffers(source, numProcessedBuffers, unqueuedAlBuffers);
			for (ALint i = 0; i < numProcessedBuffers; i++) {
				nextAvailableBufferIndex_--;
				buffersIds_[nextAvailableBufferIndex_] = unqueuedAlBuffers[i];
			}
		}

		if (audioReader_ != nullptr) {
			audioReader_->rewind();
		}
		currentBufferId_ = 0;
		AL_LOG_ERRORS();
#endif
	}

	void AudioStream::setLooping(bool value)
	{
		isLooping_ = value;

#if defined(WITH_AUDIO)
		if (audioReader_ != nullptr) {
			// The reader can't be modified while the decoding thread is using it
			theServiceLocator().GetAudioDevice().drainStreamDecode(decodeRequest_);
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
		// The old reader can't be replaced while the decoding thread is still using it
		if (decodeRequest_ == nullptr) {
			// The object was moved out, recreate the decode request
			decodeRequest_ = std::make_shared<StreamDecodeRequest>();
			decodeRequest_->buffer = std::make_unique<char[]>(BufferSize);
			decodeRequest_->bufferSize = BufferSize;
		} else {
			theServiceLocator().GetAudioDevice().drainStreamDecode(decodeRequest_);
			decodeRequest_->state.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
		}

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
		decodeRequest_->reader = audioReader_;
		audioReader_->setLooping(isLooping_);
#endif
	}
}
