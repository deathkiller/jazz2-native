#include "AudioStream.h"
#include "IAudioLoader.h"
#include "IAudioReader.h"
#include "../ServiceLocator.h"

#include <Containers/String.h>

namespace nCine
{
	// Private constructor called only by AudioStreamPlayer
	AudioStream::AudioStream()
		: _nextAvailableBufferIndex(0), _currentBufferId(0), _asyncDecodeAvailable(true), _bytesPerSample(0), _numChannels(0), _isLooping(false),
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
	std::int32_t AudioStream::unqueueProcessedBuffers(IAudioDevice& device, std::uint32_t source)
	{
#if defined(WITH_AUDIO)
		// The backend's count (AL_BUFFERS_PROCESSED) and this object's own count of what it queued
		// (_nextAvailableBufferIndex) are supposed to agree, but nothing enforces it: an upload the backend
		// rejected still counted as queued, and a buffer name that has gone stale queues nothing at all.
		// Both call sites used to hand the backend's number straight to unqueueBuffers() and then step the
		// index once per buffer, which made a disagreement memory-unsafe in two different ways:
		//
		//  - unqueueBuffers() takes no capacity and fills up to eight entries, while both callers passed a
		//    three-element stack array. In stop() that array sits in the frame that is about to return, so a
		//    count above NumBuffers overwrote the return address - a crash with nothing in the log, at a
		//    level change, which is exactly where stop() runs.
		//  - decrementing once per buffer stepped the index below zero and wrote in front of _buffersIds.
		//
		// Bounding the count here makes both impossible whatever the backend reports. It is the only safe
		// response as well as the simplest: this object owns exactly NumBuffers names, so it has nowhere to
		// put more than that many even if the backend really is holding them.
		std::int32_t count = device.numProcessedBuffers(source);
		if (count > _nextAvailableBufferIndex) {
			static bool warnedDrift = false;
			if (!warnedDrift) {
				warnedDrift = true;
				LOGW("Audio backend reported {} processed buffers with {} queued, ignoring the excess",
					count, _nextAvailableBufferIndex);
			}
			count = _nextAvailableBufferIndex;
		}
		if (count > NumBuffers) {
			count = NumBuffers;
		}
		if (count <= 0) {
			return 0;
		}

		// Zero-initialized so a rejected unqueue leaves recognizable ids rather than stack garbage, and the
		// slot then keeps the name it already held instead of taking a fabricated one
		std::uint32_t unqueuedBuffers[NumBuffers] = {};
		device.unqueueBuffers(source, count, unqueuedBuffers);
		for (std::int32_t i = 0; i < count; i++) {
			_nextAvailableBufferIndex--;
			if (unqueuedBuffers[i] != 0) {
				_buffersIds[_nextAvailableBufferIndex] = unqueuedBuffers[i];
			}
		}
		return count;
#else
		return 0;
#endif
	}

	bool AudioStream::enqueue(std::uint32_t source, bool looping)
	{
#if defined(WITH_AUDIO)
		if (_audioReader == nullptr) {
			return false;
		}

		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		// Set to false when the queue is empty and there is no more data to decode
		bool shouldKeepPlaying = true;

		// Return everything the backend has finished playing to the free list
		unqueueProcessedBuffers(device, source);

		bool reachedEndOfData = false;

		// Filling every free buffer that can be filled without decoding on this thread, which in practice is
		// the one chunk the decoding thread has ready - see below for why it stops there rather than decoding
		// the rest itself. An empty queue is the exception and does fill in a single call, so a stream starts
		// playing immediately instead of a frame later.
		while (_nextAvailableBufferIndex < NumBuffers) {
			StreamDecodeRequest::State requestState = _decodeRequest->state.load(std::memory_order_acquire);
			if (requestState == StreamDecodeRequest::State::Pending) {
				// A chunk is already being decoded elsewhere, so this thread has nothing useful to do:
				// with data still queued there is no hurry, and with the queue empty the audio has already
				// gapped and waiting cannot un-gap it. Waiting used to be the answer here, and on a frame
				// that is comfortably inside its budget it cost nothing - but it turns a slow frame into a
				// much slower one, and then into a spiral. Measured in the worst part of
				// `prince/02_castle1n` on the PSP: a frame that had fallen to 130 ms spent 55-59 ms of it
				// stopped in this wait, because the decoding thread only gets the CPU the main thread is not
				// using, so the slower the frame the less of the chunk is ready when the frame asks for it -
				// and the wait it then does makes the next frame later still. Coming back empty-handed
				// leaves the music silent for a frame and gives the decoding thread the whole of the vblank
				// wait to finish, which is what actually ends the underrun.
				break;
			}
			if (requestState != StreamDecodeRequest::State::Ready) {
				// Only ONE chunk is ever decoded ahead - the submit at the bottom of this function starts it -
				// so reaching here with buffers still queued means decoding on the CALLING thread for a queue
				// that does not need the data yet. That is what put the whole music decode inside the frame:
				// this loop fills every free buffer in one call, so after it consumes the single chunk the
				// decoding thread had ready, the next iteration found the request Idle and decoded inline.
				// Measured on the PSP at 8.1 ms per frame on 49 frames out of 60 - four fifths of the frame's
				// audio cost and a fifth of the whole frame - while the decoding thread sat unused.
				//
				// Leaving with data still queued costs nothing: the queue is drained by at most one buffer per
				// frame, and the submit below has a whole frame to refill it beside the frame rather than in
				// it. A genuine underrun (nothing queued at all) still decodes here, which is what keeps the
				// stream correct when a frame takes longer than a buffer holds.
				//
				// Only when there IS a decoding thread to hand the work to, though. Where there is not, the
				// submit below cannot start anything, so this thread is the only thing that will ever fill a
				// buffer - stopping here would leave the queue one buffer deep for the whole stream and throw
				// away the margin that carries it over a frame hitch.
				if (_nextAvailableBufferIndex > 0 && _asyncDecodeAvailable) {
					break;
				}
				_decodeRequest->looping = looping;
				_decodeRequest->Execute();
			}

			const std::int32_t bytes = _decodeRequest->bytesRead;
			_decodeRequest->state.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);

			// If it is still decoding data then enqueue
			if (bytes > 0) {
				const std::uint32_t bufferId = _buffersIds[_nextAvailableBufferIndex];
				// A buffer the backend refused to fill holds nothing to play, and queueing it anyway - and
				// counting it as queued - is what drives this object's count apart from the backend's. The
				// chunk is dropped and the same slot is retried on the next frame, which leaves the two
				// counts consistent whether the refusal was transient or permanent.
				if (!device.uploadBuffer(bufferId, _format, _decodeRequest->buffer.get(), bytes, _frequency)) {
					break;
				}
				_currentBufferId = bufferId;
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
			_asyncDecodeAvailable = device.submitStreamDecode(_decodeRequest);
			if (!_asyncDecodeAvailable) {
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

		// Stopping the source marks every queued buffer processed, so this returns all of them
		unqueueProcessedBuffers(device, source);

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
