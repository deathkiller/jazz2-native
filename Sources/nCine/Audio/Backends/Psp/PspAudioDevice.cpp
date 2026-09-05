#include "PspAudioDevice.h"

#if defined(WITH_PSPAUDIO)

#include "../../AudioMixerCommon.h"
#include "../../IAudioPlayer.h"
#include "../../../../Main.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <new>

#include <pspaudio.h>
#include <pspthreadman.h>

namespace nCine
{
	namespace
	{
		/** @brief Scoped hold of the device's kernel semaphore */
		struct SemaphoreLock
		{
			explicit SemaphoreLock(std::int32_t semaphore) : _semaphore(semaphore) {
				if (_semaphore >= 0) sceKernelWaitSema(_semaphore, 1, nullptr);
			}
			~SemaphoreLock() {
				if (_semaphore >= 0) sceKernelSignalSema(_semaphore, 1);
			}
			std::int32_t _semaphore;
		};
	}

	PspAudioDevice::PspAudioDevice()
		: _valid(false), _suspended(false), _threadShouldQuit(false), _channel(-1), _thread(-1), _lock(-1), _blocks(nullptr),
			_mixBuffer(nullptr), _mixFrequency(DefaultMixingFrequency), _lastMixedLeft(0), _lastMixedRight(0), _buffers(nullptr),
			_bufferCount(0), _bufferCapacity(0)
	{
		const std::size_t blockBytes = std::size_t(BlockFrames) * ChannelCount * sizeof(std::int16_t);
		// The hardware reads the blocks by DMA, so they are kept on their own cache lines
		_blocks = static_cast<std::int16_t*>(memalign(64, blockBytes * 2));
		_mixBuffer = static_cast<std::int32_t*>(std::malloc(std::size_t(BlockFrames) * ChannelCount * sizeof(std::int32_t)));
		_buffers = static_cast<Buffer*>(std::malloc(std::size_t(InitialBufferCapacity) * sizeof(Buffer)));
		if (_blocks == nullptr || _mixBuffer == nullptr || _buffers == nullptr) {
			LOGE("Cannot allocate the audio mixing buffers, sound will be disabled");
			return;
		}
		std::memset(_blocks, 0, blockBytes * 2);
		_bufferCapacity = InitialBufferCapacity;

		_lock = sceKernelCreateSema("nCine Audio Lock", 0, 1, 1, nullptr);
		if (_lock < 0) {
			LOGE("Cannot create the audio lock (0x{:x}), sound will be disabled", std::uint32_t(_lock));
			_lock = -1;
			return;
		}

		_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, BlockFrames, PSP_AUDIO_FORMAT_STEREO);
		if (_channel < 0) {
			LOGE("Cannot reserve an audio channel (0x{:x}), sound will be disabled", std::uint32_t(_channel));
			_channel = -1;
			return;
		}

		// Buffer id 0 is reserved as "no buffer"
		::new (&_buffers[0]) Buffer{};
		_bufferCount = 1;

		std::uint32_t sourceIds[MaxSources];
		for (std::int32_t i = 0; i < MaxSources; i++) {
			sourceIds[i] = std::uint32_t(i + 1);
		}
		setSourcePool(arrayView(sourceIds, MaxSources));

		// Above the main thread (32), so that it mixes the next block the moment the hardware takes one and
		// keeps doing so through a long frame; it sleeps in the output call otherwise. The stack is small on
		// purpose - see Thread.cpp for why thread stacks are a scarce resource on this console.
		PspAudioDevice* self = this;
		_thread = sceKernelCreateThread("nCine Audio", OutputThread, 0x10, 0x2000, PSP_THREAD_ATTR_USER, nullptr);
		if (_thread < 0 || sceKernelStartThread(_thread, sizeof(self), &self) < 0) {
			LOGE("Cannot start the audio output thread (0x{:x}), sound will be disabled", std::uint32_t(_thread));
			if (_thread >= 0) {
				sceKernelDeleteThread(_thread);
				_thread = -1;
			}
			return;
		}

		_valid = true;
		LOGI("Audio device initialized: sceAudio channel {}, mixing at {} Hz and playing {} Hz stereo in blocks of {} frames",
			_channel, _mixFrequency, OutputFrequency, BlockFrames);
	}

	PspAudioDevice::~PspAudioDevice()
	{
		shutdownDecodeThread();

		if (_thread >= 0) {
			_threadShouldQuit.store(true, std::memory_order_relaxed);
			// It is blocked in the output call for one block at most
			SceUInt timeout = 500000;
			sceKernelWaitThreadEnd(_thread, &timeout);
			sceKernelDeleteThread(_thread);
			_thread = -1;
		}
		if (_channel >= 0) {
			sceAudioChRelease(_channel);
			_channel = -1;
		}
		if (_lock >= 0) {
			sceKernelDeleteSema(_lock);
			_lock = -1;
		}

		if (_buffers != nullptr) {
			for (std::int32_t i = 0; i < _bufferCount; i++) {
				std::free(_buffers[i].Samples);
			}
			std::free(_buffers);
			_buffers = nullptr;
		}
		std::free(_mixBuffer);
		_mixBuffer = nullptr;
		std::free(_blocks);
		_blocks = nullptr;
	}

	int PspAudioDevice::OutputThread(unsigned int args, void* argp)
	{
		static_cast<void>(args);
		PspAudioDevice* device = *static_cast<PspAudioDevice**>(argp);
		const std::size_t blockSamples = std::size_t(BlockFrames) * ChannelCount;

		std::int32_t current = 0;
		while (!device->_threadShouldQuit.load(std::memory_order_relaxed)) {
			std::int16_t* block = device->_blocks + std::size_t(current) * blockSamples;
			if (device->_suspended.load(std::memory_order_relaxed)) {
				std::memset(block, 0, blockSamples * sizeof(std::int16_t));
			} else {
				SemaphoreLock lock(device->_lock);
				device->MixInto(block, BlockFrames);
			}
			// Returns once the hardware has queued the block behind the one it is playing, which paces this
			// loop at the sample rate and leaves one block of time to mix the next
			sceAudioOutputPannedBlocking(device->_channel, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, block);
			current ^= 1;
		}
		return 0;
	}

	const char* PspAudioDevice::name() const
	{
		return "sceAudio";
	}

	std::int32_t PspAudioDevice::nativeFrequency()
	{
		// The rate the sources are mixed at rather than the hardware's: the module decoder sizes itself by
		// this, and rendering it above the mixing rate would only be resampled straight back down
		return _mixFrequency;
	}

	void PspAudioDevice::setMixingFrequency(std::int32_t frequency)
	{
		// Only the rates that divide the hardware's are mixed for: the upsample in MixInto() is then a whole
		// number of output frames per mixed frame, and a block of 1024 frames splits evenly
		if (frequency != 44100 && frequency != 22050 && frequency != 11025) {
			if (frequency != 0) {
				LOGW("Cannot mix at {} Hz (only 11025, 22050 and 44100 Hz divide the hardware's rate), keeping {} Hz", frequency, _mixFrequency);
			}
			return;
		}

		SemaphoreLock lock(_lock);
		if (_mixFrequency != frequency) {
			_mixFrequency = frequency;
			_lastMixedLeft = 0;
			_lastMixedRight = 0;
			LOGI("Mixing sound at {} Hz", _mixFrequency);
		}
	}

	void PspAudioDevice::updatePlayers()
	{
		// Only the players advance here; the mixing itself happens on the output thread
		AudioDeviceBase::updatePlayers();
	}

	void PspAudioDevice::suspendDevice()
	{
		_suspended.store(true, std::memory_order_relaxed);
	}

	void PspAudioDevice::resumeDevice()
	{
		_suspended.store(false, std::memory_order_relaxed);
	}

	bool PspAudioDevice::isValid() const
	{
		return _valid;
	}

	void PspAudioDevice::setGain(float gain)
	{
		// Read by the mixer per block; a torn float is not possible on this CPU, so no lock
		_gain = gain;
	}

	void PspAudioDevice::updateListener(const Vector3f& position, const Vector3f& velocity)
	{
		static_cast<void>(velocity);
		SemaphoreLock lock(_lock);
		_listenerPos = position;
	}

	std::uint32_t PspAudioDevice::registerPlayer(IAudioPlayer* player)
	{
		SemaphoreLock lock(_lock);
		const std::uint32_t sourceId = AudioDeviceBase::registerPlayer(player);
		if (sourceId != UnavailableSource) {
			if (Source* source = GetSource(sourceId)) {
				*source = Source{};
			}
		}
		return sourceId;
	}

	PspAudioDevice::Source* PspAudioDevice::GetSource(std::uint32_t sourceId)
	{
		if (sourceId == 0 || sourceId > std::uint32_t(MaxSources)) {
			return nullptr;
		}
		return &_sources[sourceId - 1];
	}

	std::uint32_t PspAudioDevice::createBuffer(BufferUsage usage)
	{
		SemaphoreLock lock(_lock);
		static_cast<void>(usage);

		if (_buffers == nullptr) {
			return 0;
		}

		for (std::int32_t i = 1; i < _bufferCount; i++) {
			if (!_buffers[i].Used) {
				_buffers[i] = Buffer{};
				_buffers[i].Used = true;
				return std::uint32_t(i);
			}
		}

		if (_bufferCount == _bufferCapacity) {
			const std::int32_t newCapacity = _bufferCapacity * 2;
			Buffer* grown = static_cast<Buffer*>(std::realloc(_buffers, std::size_t(newCapacity) * sizeof(Buffer)));
			if (grown == nullptr) {
				LOGE("Cannot grow the audio buffer table to {} entries", newCapacity);
				return 0;
			}
			_buffers = grown;
			_bufferCapacity = newCapacity;
		}

		::new (&_buffers[_bufferCount]) Buffer{};
		_buffers[_bufferCount].Used = true;
		return std::uint32_t(_bufferCount++);
	}

	void PspAudioDevice::ReleaseBuffer(Buffer& buffer)
	{
		for (Source& source : _sources) {
			if (source.BufferId != 0 && source.BufferId < std::uint32_t(_bufferCount) && &_buffers[source.BufferId] == &buffer) {
				source.Playing = false;
				source.BufferId = 0;
			}
		}

		std::free(buffer.Samples);
		buffer.Samples = nullptr;
		buffer.Capacity = 0;
		buffer.FrameCount = 0;
	}

	void PspAudioDevice::deleteBuffer(std::uint32_t bufferId)
	{
		SemaphoreLock lock(_lock);
		if (bufferId == 0 || bufferId >= std::uint32_t(_bufferCount)) {
			return;
		}
		ReleaseBuffer(_buffers[bufferId]);
		_buffers[bufferId].Used = false;
	}

	bool PspAudioDevice::uploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency)
	{
		SemaphoreLock lock(_lock);
		if (bufferId == 0 || bufferId >= std::uint32_t(_bufferCount) || data == nullptr || size <= 0) {
			return false;
		}

		std::int32_t bytesPerSample, channelCount;
		switch (format) {
			case BufferFormat::Mono8: bytesPerSample = 1; channelCount = 1; break;
			case BufferFormat::Stereo8: bytesPerSample = 1; channelCount = 2; break;
			case BufferFormat::Mono16: bytesPerSample = 2; channelCount = 1; break;
			case BufferFormat::Stereo16: bytesPerSample = 2; channelCount = 2; break;
			default: return false;
		}

		const std::int32_t frameSize = bytesPerSample * channelCount;
		const std::int32_t frameCount = size / frameSize;
		const std::int32_t byteCount = frameCount * frameSize;

		Buffer& buffer = _buffers[bufferId];
		if (byteCount <= 0) {
			ReleaseBuffer(buffer);
			return false;
		}

		// A streaming source re-uploads several times a second, so an allocation that is already big
		// enough is kept (the same fragmentation reasoning as the N64 backend)
		if (byteCount > buffer.Capacity) {
			ReleaseBuffer(buffer);
			buffer.Samples = static_cast<std::uint8_t*>(std::malloc(std::size_t(byteCount)));
			if (buffer.Samples == nullptr) {
				LOGE("Cannot allocate {} bytes for audio buffer {}, the sound will be silent", byteCount, bufferId);
				return false;
			}
			buffer.Capacity = byteCount;
		}

		// Samples keep the width they arrived in; 8-bit arrives unsigned with 128 at silence and is
		// re-centered here, 16-bit is already native-endian (the asset readers swap on load)
		if (bytesPerSample == 1) {
			const std::uint8_t* source = static_cast<const std::uint8_t*>(data);
			std::int8_t* dest = reinterpret_cast<std::int8_t*>(buffer.Samples);
			for (std::int32_t i = 0; i < byteCount; i++) {
				dest[i] = std::int8_t(std::int32_t(source[i]) - 128);
			}
		} else {
			std::memcpy(buffer.Samples, data, std::size_t(byteCount));
		}

		buffer.BytesPerSample = bytesPerSample;
		buffer.ChannelCount = channelCount;
		buffer.Frequency = (frequency > 0 ? frequency : _mixFrequency);
		buffer.FrameCount = frameCount;
		return true;
	}

	void PspAudioDevice::setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		SemaphoreLock lock(_lock);
		if (Source* source = GetSource(sourceId)) {
			source->BufferId = bufferId;
			source->Cursor = 0;
		}
	}

	void PspAudioDevice::setSourceGain(std::uint32_t sourceId, float gain)
	{
		SemaphoreLock lock(_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Gain = gain;
		}
	}

	void PspAudioDevice::setSourcePitch(std::uint32_t sourceId, float pitch)
	{
		SemaphoreLock lock(_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Pitch = (pitch > 0.0f ? pitch : 1.0f);
		}
	}

	void PspAudioDevice::setSourceLooping(std::uint32_t sourceId, bool looping)
	{
		SemaphoreLock lock(_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Looping = looping;
		}
	}

	void PspAudioDevice::setSourceRelative(std::uint32_t sourceId, bool relative)
	{
		SemaphoreLock lock(_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Relative = relative;
		}
	}

	void PspAudioDevice::setSourcePosition(std::uint32_t sourceId, const Vector3f& position)
	{
		SemaphoreLock lock(_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Position = position;
		}
	}

	void PspAudioDevice::setSourceLowPass(std::uint32_t sourceId, float value)
	{
		// No per-voice DSP in this mixer (see the N64 backend for why the branch is not worth it)
		static_cast<void>(sourceId);
		static_cast<void>(value);
	}

	std::int32_t PspAudioDevice::sourceSampleOffset(std::uint32_t sourceId)
	{
		SemaphoreLock lock(_lock);
		Source* source = GetSource(sourceId);
		return (source != nullptr ? std::int32_t(source->Cursor >> 32) : 0);
	}

	void PspAudioDevice::setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset)
	{
		SemaphoreLock lock(_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Cursor = std::int64_t(offset > 0 ? offset : 0) << 32;
		}
	}

	void PspAudioDevice::playSource(std::uint32_t sourceId)
	{
		SemaphoreLock lock(_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Playing = true;
			source->Paused = false;
		}
	}

	void PspAudioDevice::pauseSource(std::uint32_t sourceId)
	{
		SemaphoreLock lock(_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Paused = true;
		}
	}

	void PspAudioDevice::stopSource(std::uint32_t sourceId)
	{
		SemaphoreLock lock(_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Playing = false;
			source->Paused = false;
			source->Cursor = 0;
			for (std::int32_t i = 0; i < source->QueueCount && source->ProcessedCount < MaxQueuedBuffers; i++) {
				source->Processed[source->ProcessedCount++] = source->Queue[i];
			}
			source->QueueCount = 0;
		}
	}

	bool PspAudioDevice::isSourcePlaying(std::uint32_t sourceId)
	{
		SemaphoreLock lock(_lock);
		const Source* source = GetSource(sourceId);
		return (source != nullptr && source->Playing && !source->Paused);
	}

	void PspAudioDevice::queueBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		SemaphoreLock lock(_lock);
		Source* source = GetSource(sourceId);
		if (source == nullptr || bufferId == 0 || bufferId >= std::uint32_t(_bufferCount)) {
			return;
		}
		if (source->QueueCount >= MaxQueuedBuffers) {
			LOGW("Audio source {} queue is full, dropping a buffer", sourceId);
			return;
		}
		source->Queue[source->QueueCount++] = bufferId;
	}

	std::int32_t PspAudioDevice::numProcessedBuffers(std::uint32_t sourceId)
	{
		SemaphoreLock lock(_lock);
		const Source* source = GetSource(sourceId);
		return (source != nullptr ? source->ProcessedCount : 0);
	}

	void PspAudioDevice::unqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds)
	{
		SemaphoreLock lock(_lock);
		Source* source = GetSource(sourceId);
		if (source == nullptr || count <= 0) {
			return;
		}
		if (count > source->ProcessedCount) {
			count = source->ProcessedCount;
		}
		if (bufferIds != nullptr) {
			for (std::int32_t i = 0; i < count; i++) {
				bufferIds[i] = source->Processed[i];
			}
		}
		source->ProcessedCount -= count;
		for (std::int32_t i = 0; i < source->ProcessedCount; i++) {
			source->Processed[i] = source->Processed[i + count];
		}
	}

	PspAudioDevice::Buffer* PspAudioDevice::GetActiveBuffer(Source& source)
	{
		const std::uint32_t bufferId = (source.QueueCount > 0 ? source.Queue[0] : source.BufferId);
		if (bufferId == 0 || bufferId >= std::uint32_t(_bufferCount)) {
			return nullptr;
		}
		Buffer& buffer = _buffers[bufferId];
		return (buffer.Used && buffer.Samples != nullptr && buffer.FrameCount > 0 ? &buffer : nullptr);
	}

	DEATH_ALWAYS_INLINE void PspAudioDevice::ReadFrame(const Buffer& buffer, std::int32_t frame, std::int32_t& left, std::int32_t& right)
	{
		const std::int32_t index = frame * buffer.ChannelCount;
		if (buffer.BytesPerSample == 1) {
			const std::int8_t* samples = reinterpret_cast<const std::int8_t*>(buffer.Samples);
			left = std::int32_t(samples[index]) << 8;
			right = (buffer.ChannelCount == 2 ? std::int32_t(samples[index + 1]) << 8 : left);
		} else {
			const std::int16_t* samples = reinterpret_cast<const std::int16_t*>(buffer.Samples);
			left = samples[index];
			right = (buffer.ChannelCount == 2 ? samples[index + 1] : left);
		}
	}

	void PspAudioDevice::ComputePanning(const Source& source, float& leftGain, float& rightGain) const
	{
		AudioMixer::ComputeStereoGains(source.Relative, source.Position, _listenerPos, source.Gain, _gain, leftGain, rightGain);
	}

	bool PspAudioDevice::MixSource(Source& source, std::int32_t* output, std::int32_t frames)
	{
		Buffer* buffer = GetActiveBuffer(source);
		if (buffer == nullptr) {
			return false;
		}

		// Float only per block: the gains become Q15, the cursor step 32.32 - the per-sample loop is
		// all integer (the reasoning is spelled out in the N64 backend, which this mixer is a copy of)
		float leftGain, rightGain;
		ComputePanning(source, leftGain, rightGain);
		const std::int32_t leftQ15 = std::int32_t(leftGain * 32768.0f + 0.5f);
		const std::int32_t rightQ15 = std::int32_t(rightGain * 32768.0f + 0.5f);

		std::int64_t step = std::int64_t((double(buffer->Frequency) / double(_mixFrequency)) * double(source.Pitch) * 4294967296.0);
		std::int64_t end = std::int64_t(buffer->FrameCount) << 32;

		for (std::int32_t i = 0; i < frames; i++) {
			while (source.Cursor >= end) {
				if (source.QueueCount > 0) {
					if (source.ProcessedCount < MaxQueuedBuffers) {
						source.Processed[source.ProcessedCount++] = source.Queue[0];
					}
					for (std::int32_t q = 1; q < source.QueueCount; q++) {
						source.Queue[q - 1] = source.Queue[q];
					}
					source.QueueCount--;
					source.Cursor -= end;
					buffer = GetActiveBuffer(source);
					if (buffer == nullptr) {
						return false;
					}
					step = std::int64_t((double(buffer->Frequency) / double(_mixFrequency)) * double(source.Pitch) * 4294967296.0);
					end = std::int64_t(buffer->FrameCount) << 32;
				} else if (source.Looping) {
					source.Cursor %= end;
				} else {
					return false;
				}
			}

			const std::int32_t index = std::int32_t(source.Cursor >> 32);
			const std::int32_t fraction = std::int32_t((source.Cursor >> 24) & 0xFF);
			const std::int32_t nextIndex = (index + 1 < buffer->FrameCount ? index + 1 : index);

			std::int32_t l0, r0, l1, r1;
			ReadFrame(*buffer, index, l0, r0);
			ReadFrame(*buffer, nextIndex, l1, r1);
			const std::int32_t left = l0 + (((l1 - l0) * fraction) >> 8);
			const std::int32_t right = r0 + (((r1 - r0) * fraction) >> 8);

			output[i * ChannelCount] += (left * leftQ15) >> 15;
			output[i * ChannelCount + 1] += (right * rightQ15) >> 15;
			source.Cursor += step;
		}
		return true;
	}

	void PspAudioDevice::MixInto(std::int16_t* output, std::int32_t frames)
	{
		// Silence is the common case and skips the accumulator entirely (see the N64 backend)
		bool anyPlaying = false;
		for (const Source& source : _sources) {
			if (source.Playing && !source.Paused) {
				anyPlaying = true;
				break;
			}
		}
		if (!anyPlaying) {
			std::memset(output, 0, std::size_t(frames) * ChannelCount * sizeof(std::int16_t));
			_lastMixedLeft = 0;
			_lastMixedRight = 0;
			return;
		}

		// The sources are mixed at _mixFrequency, which divides the hardware's rate (see setMixingFrequency()),
		// so a block of `frames` output frames is `frames / ratio` mixed frames
		const std::int32_t ratio = OutputFrequency / _mixFrequency;
		const std::int32_t mixFrames = frames / ratio;

		std::int32_t* accumulator = _mixBuffer;
		std::memset(accumulator, 0, std::size_t(mixFrames) * ChannelCount * sizeof(std::int32_t));

		for (Source& source : _sources) {
			if (!source.Playing || source.Paused) {
				continue;
			}
			if (!MixSource(source, accumulator, mixFrames)) {
				source.Playing = false;
				source.Cursor = 0;
			}
		}

		if (ratio == 1) {
			// Clamp the wide accumulator into the device's 16 bits
			const std::int32_t total = frames * ChannelCount;
			for (std::int32_t i = 0; i < total; i++) {
				std::int32_t value = accumulator[i];
				value = (value < -32768 ? -32768 : (value > 32767 ? 32767 : value));
				output[i] = std::int16_t(value);
			}
			return;
		}

		// Upsample to the hardware's rate: each output frame is interpolated between the previous mixed frame
		// and the current one, so the last frame of the previous block is carried over rather than the first of
		// this one being repeated. The ratio is 2 or 4, so the division is a shift. One mixed frame of delay
		// (45 us at most) is the price, and the interpolation is what keeps the doubled samples from imaging
		// as a harsh top end the way a plain repeat would.
		const std::int32_t shift = (ratio == 4 ? 2 : 1);
		std::int32_t previousLeft = _lastMixedLeft;
		std::int32_t previousRight = _lastMixedRight;
		std::int32_t outIndex = 0;
		for (std::int32_t i = 0; i < mixFrames; i++) {
			std::int32_t left = accumulator[i * ChannelCount];
			std::int32_t right = accumulator[i * ChannelCount + 1];
			left = (left < -32768 ? -32768 : (left > 32767 ? 32767 : left));
			right = (right < -32768 ? -32768 : (right > 32767 ? 32767 : right));
			const std::int32_t deltaLeft = left - previousLeft;
			const std::int32_t deltaRight = right - previousRight;
			for (std::int32_t k = 1; k <= ratio; k++) {
				output[outIndex++] = std::int16_t(previousLeft + ((deltaLeft * k) >> shift));
				output[outIndex++] = std::int16_t(previousRight + ((deltaRight * k) >> shift));
			}
			previousLeft = left;
			previousRight = right;
		}
		_lastMixedLeft = previousLeft;
		_lastMixedRight = previousRight;
	}
}

#endif
