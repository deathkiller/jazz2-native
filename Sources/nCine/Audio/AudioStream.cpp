#include "AudioStream.h"
#if !defined(NCINE_HAS_NATIVE_AUDIO)
#	include "IAudioLoader.h"
#	include "IAudioReader.h"
#endif
#include "../ServiceLocator.h"

#include <algorithm>

#include <Containers/String.h>

namespace nCine
{
	// Private constructor called only by AudioStreamPlayer
	AudioStream::AudioStream(StringView filename)
		: AudioStream()
	{
#if defined(WITH_AUDIO)
		const bool hasLoaded = LoadFromFile(filename);
		if (!hasLoaded) {
			LOGE("Audio file \"{}\" cannot be loaded", filename);
		}
#endif
	}

	AudioStream::AudioStream(AudioStream&&) = default;
	AudioStream& AudioStream::operator=(AudioStream&&) = default;

#if defined(NCINE_HAS_NATIVE_AUDIO)
	// Private constructor called only by AudioStreamPlayer
	AudioStream::AudioStream()
		: _bytesPerSample(0), _numChannels(0), _frequency(0), _numSamples(0), _duration(0.0f), _isLooping(false)
	{
	}

	// The handle closes the stream by itself
	AudioStream::~AudioStream() = default;

	AudioStream::NativeStreamHandle::~NativeStreamHandle()
	{
#	if defined(WITH_AUDIO)
		if (Id != 0) {
			theServiceLocator().GetAudioDevice().CloseNativeStream(Id);
		}
#	endif
	}

	bool AudioStream::Enqueue(std::uint32_t source, bool looping)
	{
#	if defined(WITH_AUDIO)
		if (_nativeStream.Id == 0) {
			return false;
		}

		// Nothing to decode: the device plays the stream itself once it is bound to the source, and all that is
		// left to do here is to keep the looping flag in step and report whether it still plays
		IAudioDevice& device = theServiceLocator().GetAudioDevice();
		device.SetSourceLooping(source, looping);
		if (!_nativeStream.Started) {
			_nativeStream.Started = true;
			device.SetSourceNativeStream(source, _nativeStream.Id);
			device.PlaySource(source);
		}
		return device.IsSourcePlaying(source);
#	else
		return false;
#	endif
	}

	void AudioStream::Stop(std::uint32_t source)
	{
#	if defined(WITH_AUDIO)
		// The device rewinds a native stream when its source is stopped; the next Play() binds it again,
		// possibly to another source
		theServiceLocator().GetAudioDevice().StopSource(source);
		_nativeStream.Started = false;
#	endif
	}

	void AudioStream::SetLooping(bool value)
	{
		// The device takes the flag from every Enqueue()
		_isLooping = value;
	}

	// The device plays the file by itself, the engine doesn't see a tracker module at all
	bool AudioStream::GetModuleState(AudioModuleState& state) const
	{
		return false;
	}

	bool AudioStream::SetModulePosition(std::uint32_t source, std::int32_t order, std::int32_t row, bool restart, bool looping)
	{
		return false;
	}

	bool AudioStream::SetModuleSpeed(std::int32_t speed)
	{
		return false;
	}

	bool AudioStream::SetModuleTempo(std::int32_t tempo)
	{
		return false;
	}

	bool AudioStream::SlideModuleChannelVolume(std::int32_t channel, float volume, std::int32_t milliseconds)
	{
		return false;
	}

	bool AudioStream::LoadFromFile(StringView filename)
	{
#	if defined(WITH_AUDIO)
		// Replacing the handle closes the previous stream, also when this file cannot be played
		IAudioDevice::NativeAudioInfo info;
		_nativeStream = NativeStreamHandle(theServiceLocator().GetAudioDevice().OpenNativeStream(filename, info));
		if (_nativeStream.Id != 0) {
			_bytesPerSample = info.BytesPerSample;
			_numChannels = info.NumChannels;
			_frequency = info.Frequency;
			_numSamples = info.NumSamples;
			_duration = (_numSamples < 0 || _frequency <= 0 ? -1.0f : float(_numSamples) / _frequency);
			return true;
		}
#	endif
		return false;
	}
#else
	// Private constructor called only by AudioStreamPlayer
	AudioStream::AudioStream()
		: _nextAvailableBufferIndex(0), _currentBufferId(0), _asyncDecodeAvailable(true), _bytesPerSample(0), _numChannels(0), _isLooping(false),
			_frequency(0), _numSamples(0), _duration(0.0f), _format(IAudioDevice::BufferFormat::Mono16), _buffersIds(NumBuffers)
	{
#	if defined(WITH_AUDIO)
		IAudioDevice& device = theServiceLocator().GetAudioDevice();
		for (std::int32_t i = 0; i < NumBuffers; i++) {
			_buffersIds[i] = device.CreateBuffer(IAudioDevice::BufferUsage::Streaming);
			if DEATH_UNLIKELY(_buffersIds[i] == 0) {
				LOGW("Cannot create streaming audio buffer");
			}
		}
		_decodeRequest = std::make_shared<StreamDecodeRequest>();
		_decodeRequest->Buffer = std::make_unique<char[]>(BufferSize);
		_decodeRequest->BufferSize = BufferSize;
#	endif
	}

	AudioStream::~AudioStream()
	{
#	if defined(WITH_AUDIO)
		// Don't delete buffers if this is a moved out object
		if (_buffersIds.size() == NumBuffers) {
			IAudioDevice& device = theServiceLocator().GetAudioDevice();
			for (std::int32_t i = 0; i < NumBuffers; i++) {
				device.DeleteBuffer(_buffersIds[i]);
			}
		}
#	endif
	}

	std::int32_t AudioStream::GetStreamSampleCount() const
	{
#	if defined(WITH_AUDIO)
		if (_numChannels * _bytesPerSample > 0) {
			return BufferSize / (_numChannels * _bytesPerSample);
		}
#	endif
		return 0UL;
	}

	// Returns false once the stream has been entirely decoded and played
	std::int32_t AudioStream::UnqueueProcessedBuffers(IAudioDevice& device, std::uint32_t source)
	{
#	if defined(WITH_AUDIO)
		// Bounded by the NumBuffers names this object owns: the backend count can disagree with ours
		// (rejected uploads, stale names), and trusting it overflowed the caller's array
		std::int32_t count = device.GetProcessedBufferCount(source);
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
		device.UnqueueBuffers(source, count, unqueuedBuffers);
		for (std::int32_t i = 0; i < count; i++) {
			_nextAvailableBufferIndex--;
			if (unqueuedBuffers[i] != 0) {
				_buffersIds[_nextAvailableBufferIndex] = unqueuedBuffers[i];
			}
		}
		PopQueuedChunks(count);
		return count;
#	else
		return 0;
#	endif
	}

	bool AudioStream::Enqueue(std::uint32_t source, bool looping)
	{
#	if defined(WITH_AUDIO)
		if (_audioReader == nullptr) {
			return false;
		}

		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		// Set to false when the queue is empty and there is no more data to decode
		bool shouldKeepPlaying = true;

		// Return everything the backend has finished playing to the free list
		UnqueueProcessedBuffers(device, source);

		bool reachedEndOfData = false;

		// Fill free buffers only with chunks already decoded, an empty queue is filled at once
		// so the stream starts immediately
		while (_nextAvailableBufferIndex < NumBuffers) {
			StreamDecodeRequest::State requestState = _decodeRequest->CurrentState.load(std::memory_order_acquire);
			if (requestState == StreamDecodeRequest::State::Pending) {
				// The chunk is still being decoded elsewhere, don't wait for it - waiting turns a slow frame
				// into a spiral (the decoding thread only gets the CPU the main thread leaves)
				break;
			}
			if (requestState != StreamDecodeRequest::State::Ready) {
				// Only one chunk is decoded ahead, so with buffers still queued don't decode on this thread,
				// the submit below refills the queue outside the frame. Without a decoding thread, keep filling here.
				if (_nextAvailableBufferIndex > 0 && _asyncDecodeAvailable) {
					break;
				}
				_decodeRequest->Looping = looping;
				_decodeRequest->Execute();
			}

			const std::int32_t bytes = _decodeRequest->BytesRead;
			_decodeRequest->CurrentState.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);

			// If it is still decoding data then enqueue
			if (bytes > 0) {
				const std::uint32_t bufferId = _buffersIds[_nextAvailableBufferIndex];
				// Drop a chunk the backend refused and retry the slot next frame, so the queue count stays in sync
				if (!device.UploadBuffer(bufferId, _format, _decodeRequest->Buffer.get(), bytes, _frequency)) {
					break;
				}
				_currentBufferId = bufferId;
				device.QueueBuffer(source, _currentBufferId);
				_nextAvailableBufferIndex++;
				PushQueuedChunk(bytes);
			} else {
				reachedEndOfData = true;
				// If there is no more data left to decode and the queue is empty
				if (_nextAvailableBufferIndex == 0) {
					shouldKeepPlaying = false;
					Stop(source);
				}
				break;
			}
		}

		// Decode the next chunk ahead of time on the decoding thread while the queued buffers play
		if (shouldKeepPlaying && !reachedEndOfData &&
			_decodeRequest->CurrentState.load(std::memory_order_relaxed) == StreamDecodeRequest::State::Idle) {
			_decodeRequest->Looping = looping;
			// A device that decodes the chunk in slices continues from what is already in the buffer
			_decodeRequest->BytesRead = 0;
			_decodeRequest->CurrentState.store(StreamDecodeRequest::State::Pending, std::memory_order_relaxed);
			_asyncDecodeAvailable = device.SubmitStreamDecode(_decodeRequest);
			if (!_asyncDecodeAvailable) {
				// No decoding thread is available, the next chunk will be decoded synchronously instead
				_decodeRequest->CurrentState.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
			}
		}

		// Handle buffer underrun case: `_nextAvailableBufferIndex` is the number of buffers currently
		// queued on the source, tracked here instead of queried from the backend
		if (_nextAvailableBufferIndex > 0 && !device.IsSourcePlaying(source)) {
			// Need to restart play
			device.PlaySource(source);
		}

		return shouldKeepPlaying;
#	else
		return false;
#	endif
	}

	void AudioStream::Stop(std::uint32_t source)
	{
#	if defined(WITH_AUDIO)
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		// The reader can't be rewound while the decoding thread is using it,
		// any chunk decoded ahead of time is stale after the rewind anyway
		if (_decodeRequest != nullptr) {
			device.DrainStreamDecode(_decodeRequest);
			_decodeRequest->CurrentState.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
		}

		// In order to unqueue all the buffers, the source must be stopped first
		device.StopSource(source);

		// Stopping the source marks every queued buffer processed, so this returns all of them
		UnqueueProcessedBuffers(device, source);

		if (_audioReader != nullptr) {
			_audioReader->Rewind();
		}
		_currentBufferId = 0;
		_queuedChunkCount = 0;
#	endif
	}

	bool AudioStream::GetModuleState(AudioModuleState& state) const
	{
#	if defined(WITH_AUDIO)
		if (_audioReader == nullptr) {
			return false;
		}

		// The frame being heard: the first queued buffer is the one playing, and how far it got is told by the time
		// since it started, which is noticed one frame late at worst
		std::int64_t frame;
		if (_queuedChunkCount > 0) {
			const QueuedChunk& chunk = _queuedChunks[_firstQueuedChunk];
			std::int64_t elapsed = std::int64_t(_firstChunkStartTime.secondsSince() * float(_frequency));
			frame = chunk.StartFrame + std::clamp<std::int64_t>(elapsed, 0, std::max<std::int64_t>(chunk.FrameCount - 1, 0));
		} else {
			frame = _audioReader->GetDecodedFrames();
		}
		return _audioReader->GetModuleStateAt(frame, state);
#	else
		return false;
#	endif
	}

	bool AudioStream::SetModulePosition(std::uint32_t source, std::int32_t order, std::int32_t row, bool restart, bool looping)
	{
#	if defined(WITH_AUDIO)
		if (_audioReader == nullptr || !_audioReader->CanSetModulePosition() || order < 0 || row < 0) {
			return false;
		}

		// The chunk decoded ahead is from before the jump, it's dropped
		IAudioDevice& device = theServiceLocator().GetAudioDevice();
		if (_decodeRequest != nullptr) {
			device.DrainStreamDecode(_decodeRequest);
			_decodeRequest->CurrentState.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
		}

		if (!_audioReader->SetModulePosition(order, row)) {
			return false;
		}

		// The queued buffers are from before the jump too, also when paused (the queue is filled again on resume)
		if (_nextAvailableBufferIndex > 0) {
			// Stopping the source marks every queued buffer processed
			device.StopSource(source);
			UnqueueProcessedBuffers(device, source);
			_queuedChunkCount = 0;
		}
		if (restart) {
			Enqueue(source, looping);
		}
		return true;
#	else
		return false;
#	endif
	}

	bool AudioStream::SetModuleSpeed(std::int32_t speed)
	{
		return (_audioReader != nullptr && _audioReader->SetModuleSpeed(speed));
	}

	bool AudioStream::SetModuleTempo(std::int32_t tempo)
	{
		return (_audioReader != nullptr && _audioReader->SetModuleTempo(tempo));
	}

	bool AudioStream::SlideModuleChannelVolume(std::int32_t channel, float volume, std::int32_t milliseconds)
	{
		return (_audioReader != nullptr && _audioReader->SlideModuleChannelVolume(channel, volume, milliseconds));
	}

	void AudioStream::PushQueuedChunk(std::int32_t bytes)
	{
		if (_queuedChunkCount >= NumBuffers) {
			return;
		}

		std::int32_t frameSize = _numChannels * _bytesPerSample;
		std::int32_t frames = (frameSize > 0 ? bytes / frameSize : 0);
		if (_queuedChunkCount == 0) {
			_firstChunkStartTime = TimeStamp::now();
		}
		// Nothing else is decoding now, so the reader stopped right at the end of this chunk
		QueuedChunk& chunk = _queuedChunks[(_firstQueuedChunk + _queuedChunkCount) % NumBuffers];
		chunk.StartFrame = _audioReader->GetDecodedFrames() - frames;
		chunk.FrameCount = frames;
		_queuedChunkCount++;
	}

	void AudioStream::PopQueuedChunks(std::int32_t count)
	{
		count = std::min(count, _queuedChunkCount);
		if (count <= 0) {
			return;
		}

		_firstQueuedChunk = (_firstQueuedChunk + count) % NumBuffers;
		_queuedChunkCount -= count;
		_firstChunkStartTime = TimeStamp::now();
	}

	void AudioStream::SetLooping(bool value)
	{
		_isLooping = value;

#	if defined(WITH_AUDIO)
		if (_audioReader != nullptr) {
			// The reader can't be modified while the decoding thread is using it
			theServiceLocator().GetAudioDevice().DrainStreamDecode(_decodeRequest);
			_audioReader->SetLooping(value);
		}
#	endif
	}

	bool AudioStream::LoadFromFile(StringView filename)
	{
#	if defined(WITH_AUDIO)
		std::unique_ptr<IAudioLoader> audioLoader = IAudioLoader::CreateFromFile(filename);
		if (audioLoader->HasLoaded()) {
			CreateReader(*audioLoader);
			return true;
		}	
#	endif
		return false;
	}

	void AudioStream::CreateReader(IAudioLoader& audioLoader)
	{
#	if defined(WITH_AUDIO)
		// The old reader can't be replaced while the decoding thread is still using it
		if (_decodeRequest == nullptr) {
			// The object was moved out, recreate the decode request
			_decodeRequest = std::make_shared<StreamDecodeRequest>();
			_decodeRequest->Buffer = std::make_unique<char[]>(BufferSize);
			_decodeRequest->BufferSize = BufferSize;
		} else {
			theServiceLocator().GetAudioDevice().DrainStreamDecode(_decodeRequest);
			_decodeRequest->CurrentState.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
		}

		_bytesPerSample = audioLoader.GetBytesPerSample();
		_numChannels = audioLoader.GetChannelCount();

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

		_frequency = audioLoader.GetFrequency();
		_numSamples = audioLoader.GetSampleCount();
		_duration = (_numSamples == UINT32_MAX ? -1.0f : float(_numSamples) / _frequency);

		_audioReader = audioLoader.CreateReader();
		_decodeRequest->Reader = _audioReader;
		_audioReader->SetLooping(_isLooping);
#	endif
	}
#endif
}
