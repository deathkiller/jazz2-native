#include "AudioStream.h"
#include "IAudioLoader.h"
#include "IAudioReader.h"
#include "../ServiceLocator.h"

#include <Containers/String.h>

namespace nCine
{
	// Private constructor called only by AudioStreamPlayer
	AudioStream::AudioStream()
		: _nextAvailableBufferIndex(0), _currentBufferId(0), _bytesPerSample(0), _numChannels(0), _isLooping(false),
			_frequency(0), _numSamples(0), _duration(0.0f), _format(IAudioDevice::BufferFormat::Mono16), _buffersIds(NumBuffers)
	{
#if defined(WITH_AUDIO)
		IAudioDevice& device = theServiceLocator().GetAudioDevice();
		for (std::int32_t i = 0; i < NumBuffers; i++) {
			_buffersIds[i] = device.createBuffer(IAudioDevice::BufferUsage::Streaming);
			if DEATH_UNLIKELY(_buffersIds[i] == 0) {
				LOGW("Cannot create streaming audio buffer");
			}
		}
		_decodeRequest = std::make_shared<StreamDecodeRequest>();
		_decodeRequest->buffer = std::make_unique<char[]>(BufferSize);
		_decodeRequest->bufferSize = BufferSize;
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
		if (_buffersIds.size() == NumBuffers) {
			IAudioDevice& device = theServiceLocator().GetAudioDevice();
			for (std::int32_t i = 0; i < NumBuffers; i++) {
				device.deleteBuffer(_buffersIds[i]);
			}
		}
#endif
	}

	AudioStream::AudioStream(AudioStream&&) = default;
	AudioStream& AudioStream::operator=(AudioStream&&) = default;

	std::int32_t AudioStream::numStreamSamples() const
	{
#if defined(WITH_AUDIO)
		if (_numChannels * _bytesPerSample > 0) {
			return BufferSize / (_numChannels * _bytesPerSample);
		}
#endif
		return 0UL;
	}

	// Returns false once the stream has been entirely decoded and played
	bool AudioStream::enqueue(std::uint32_t source, bool looping)
	{
#if defined(WITH_AUDIO)
		if (_audioReader == nullptr) {
			return false;
		}

		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		// Set to false when the queue is empty and there is no more data to decode
		bool shouldKeepPlaying = true;

		const std::int32_t numProcessedBuffers = device.numProcessedBuffers(source);

		// Unqueueing all processed buffers with a single call
		if (numProcessedBuffers > 0) {
			std::uint32_t unqueuedBuffers[NumBuffers];
			device.unqueueBuffers(source, numProcessedBuffers, unqueuedBuffers);
			for (std::int32_t i = 0; i < numProcessedBuffers; i++) {
				_nextAvailableBufferIndex--;
				_buffersIds[_nextAvailableBufferIndex] = unqueuedBuffers[i];
			}
		}

		bool reachedEndOfData = false;

		// Queueing until all available buffers are filled, so the queue is primed
		// in a single call and short frame hitches don't cause an underrun
		while (_nextAvailableBufferIndex < NumBuffers) {
			StreamDecodeRequest::State requestState = _decodeRequest->state.load(std::memory_order_acquire);
			if (requestState == StreamDecodeRequest::State::Pending) {
				if (_nextAvailableBufferIndex > 0) {
					// The next chunk is still being decoded, but there is queued data left to play, try again next frame
					break;
				}
				// The queue is empty (stream start or underrun after a long hitch), the data is needed right now
				device.drainStreamDecode(_decodeRequest);
				requestState = _decodeRequest->state.load(std::memory_order_acquire);
			}
			if (requestState != StreamDecodeRequest::State::Ready) {
				// Decode synchronously - the first fill after a start, or no decoding thread is available
				_decodeRequest->looping = looping;
				_decodeRequest->Execute();
			}

			const std::int32_t bytes = _decodeRequest->bytesRead;
			_decodeRequest->state.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);

			// If it is still decoding data then enqueue
			if (bytes > 0) {
				_currentBufferId = _buffersIds[_nextAvailableBufferIndex];
				device.uploadBuffer(_currentBufferId, _format, _decodeRequest->buffer.get(), bytes, _frequency);
				device.queueBuffer(source, _currentBufferId);
				_nextAvailableBufferIndex++;
			} else {
				reachedEndOfData = true;
				// If there is no more data left to decode and the queue is empty
				if (_nextAvailableBufferIndex == 0) {
					shouldKeepPlaying = false;
					stop(source);
				}
				break;
			}
		}

		// Decode the next chunk ahead of time on the decoding thread while the queued buffers play
		if (shouldKeepPlaying && !reachedEndOfData &&
			_decodeRequest->state.load(std::memory_order_relaxed) == StreamDecodeRequest::State::Idle) {
			_decodeRequest->looping = looping;
			_decodeRequest->state.store(StreamDecodeRequest::State::Pending, std::memory_order_relaxed);
			if (!device.submitStreamDecode(_decodeRequest)) {
				// No decoding thread is available, the next chunk will be decoded synchronously instead
				_decodeRequest->state.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
			}
		}

		// Handle buffer underrun case: `_nextAvailableBufferIndex` is the number of buffers currently
		// queued on the source, tracked here instead of queried from the backend
		if (_nextAvailableBufferIndex > 0 && !device.isSourcePlaying(source)) {
			// Need to restart play
			device.playSource(source);
		}

		return shouldKeepPlaying;
#else
		return false;
#endif
	}

	void AudioStream::stop(std::uint32_t source)
	{
#if defined(WITH_AUDIO)
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		// The reader can't be rewound while the decoding thread is using it,
		// any chunk decoded ahead of time is stale after the rewind anyway
		if (_decodeRequest != nullptr) {
			device.drainStreamDecode(_decodeRequest);
			_decodeRequest->state.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
		}

		// In order to unqueue all the buffers, the source must be stopped first
		device.stopSource(source);

		const std::int32_t numProcessedBuffers = device.numProcessedBuffers(source);

		// Unqueueing all processed buffers with a single call
		if (numProcessedBuffers > 0) {
			std::uint32_t unqueuedBuffers[NumBuffers];
			device.unqueueBuffers(source, numProcessedBuffers, unqueuedBuffers);
			for (std::int32_t i = 0; i < numProcessedBuffers; i++) {
				_nextAvailableBufferIndex--;
				_buffersIds[_nextAvailableBufferIndex] = unqueuedBuffers[i];
			}
		}

		if (_audioReader != nullptr) {
			_audioReader->rewind();
		}
		_currentBufferId = 0;
#endif
	}

	void AudioStream::setLooping(bool value)
	{
		_isLooping = value;

#if defined(WITH_AUDIO)
		if (_audioReader != nullptr) {
			// The reader can't be modified while the decoding thread is using it
			theServiceLocator().GetAudioDevice().drainStreamDecode(_decodeRequest);
			_audioReader->setLooping(value);
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
		if (_decodeRequest == nullptr) {
			// The object was moved out, recreate the decode request
			_decodeRequest = std::make_shared<StreamDecodeRequest>();
			_decodeRequest->buffer = std::make_unique<char[]>(BufferSize);
			_decodeRequest->bufferSize = BufferSize;
		} else {
			theServiceLocator().GetAudioDevice().drainStreamDecode(_decodeRequest);
			_decodeRequest->state.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
		}

		_bytesPerSample = audioLoader.bytesPerSample();
		_numChannels = audioLoader.numChannels();

		if (_numChannels == 1) {
			_format = (_bytesPerSample == 2 ? IAudioDevice::BufferFormat::Mono16 : IAudioDevice::BufferFormat::Mono8);
		} else if (_numChannels == 2) {
			_format = (_bytesPerSample == 2 ? IAudioDevice::BufferFormat::Stereo16 : IAudioDevice::BufferFormat::Stereo8);
		} else {
			_bytesPerSample = 0;
			_numChannels = 0;
			LOGE("Audio stream with {} channels is not supported", _numChannels);
			return;
		}

		_frequency = audioLoader.frequency();
		_numSamples = audioLoader.numSamples();
		_duration = (_numSamples == UINT32_MAX ? -1.0f : float(_numSamples) / _frequency);

		_audioReader = audioLoader.createReader();
		_decodeRequest->reader = _audioReader;
		_audioReader->setLooping(_isLooping);
#endif
	}
}
