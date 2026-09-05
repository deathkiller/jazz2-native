#include "NdspAudioDevice.h"

#if defined(WITH_NDSP)

#include "../../AudioMixerCommon.h"
#include "../../IAudioPlayer.h"
#include "../../../../Main.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

#include <3ds.h>

namespace nCine
{
	namespace
	{
		/** @brief Scoped hold of the device's lock */
		struct LockGuard
		{
			explicit LockGuard(LightLock* lock) : _lock(lock) {
				LightLock_Lock(_lock);
			}
			~LockGuard() {
				LightLock_Unlock(_lock);
			}
			LightLock* _lock;
		};
	}

	NdspAudioDevice::NdspAudioDevice()
		: _valid(false), _ndspInitialized(false), _suspended(false), _threadShouldQuit(false), _thread(nullptr),
			_blocks(nullptr), _waveBufs{}, _mixBuffer(nullptr), _mixFrequency(DefaultMixingFrequency), _buffers(nullptr),
			_bufferCount(0), _bufferCapacity(0)
	{
		LightLock_Init(&_lock);
		LightEvent_Init(&_blockDone, RESET_ONESHOT);

		// The firmware the DSP runs is loaded from the SD card here (see the class documentation); without it
		// there is no sound on this console, but there is still a game
		const ::Result rc = ndspInit();
		if (R_FAILED(rc)) {
			LOGE("Cannot initialize NDSP (0x{:x}), sound will be disabled - the DSP firmware \"sdmc:/3ds/dspfirm.cdc\" is probably missing", std::uint32_t(rc));
			return;
		}
		_ndspInitialized = true;

		const std::size_t blockBytes = std::size_t(BlockFrames) * ChannelCount * sizeof(std::int16_t);
		// The DSP reads the wave buffers by DMA out of the linear heap, the same memory the GPU's textures are in
		_blocks = static_cast<std::int16_t*>(linearAlloc(blockBytes * BlockCount));
		_mixBuffer = static_cast<std::int32_t*>(std::malloc(std::size_t(BlockFrames) * ChannelCount * sizeof(std::int32_t)));
		_buffers = static_cast<Buffer*>(std::malloc(std::size_t(InitialBufferCapacity) * sizeof(Buffer)));
		if (_blocks == nullptr || _mixBuffer == nullptr || _buffers == nullptr) {
			LOGE("Cannot allocate the audio mixing buffers, sound will be disabled");
			return;
		}
		std::memset(_blocks, 0, blockBytes * BlockCount);
		_bufferCapacity = InitialBufferCapacity;

		// Buffer id 0 is reserved as "no buffer"
		::new (&_buffers[0]) Buffer{};
		_bufferCount = 1;

		std::uint32_t sourceIds[MaxSources];
		for (std::int32_t i = 0; i < MaxSources; i++) {
			sourceIds[i] = std::uint32_t(i + 1);
		}
		setSourcePool(arrayView(sourceIds, MaxSources));

		ndspSetOutputMode(NDSP_OUTPUT_STEREO);
		ndspChnReset(Channel);
		ndspChnSetInterp(Channel, NDSP_INTERP_LINEAR);
		ndspChnSetRate(Channel, float(_mixFrequency));
		ndspChnSetFormat(Channel, NDSP_FORMAT_STEREO_PCM16);
		float mix[12] = {};
		mix[0] = 1.0f;	// Front left
		mix[1] = 1.0f;	// Front right
		ndspChnSetMix(Channel, mix);

		// The ring starts full of silence, so the DSP has something to play while the first real block is mixed
		for (std::int32_t i = 0; i < BlockCount; i++) {
			_waveBufs[i].data_vaddr = _blocks + std::size_t(i) * BlockFrames * ChannelCount;
			_waveBufs[i].nsamples = BlockFrames;
			DSP_FlushDataCache(_waveBufs[i].data_vaddr, blockBytes);
			ndspChnWaveBufAdd(Channel, &_waveBufs[i]);
		}

		// The DSP finishes a wave buffer every BlockFrames frames and the callback runs once per DSP frame
		// (every 4.8 ms), which is what wakes the mixer below
		ndspSetCallback(FrameCallback, this);

		// Above the main thread (0x30), so that it mixes the next block the moment the DSP hands one back and
		// keeps doing so through a long frame; it sleeps in the event wait otherwise. On the same core as the
		// game, like every thread of the engine here (see CtrPthread.cpp); the mixer's stack is small on purpose.
		_thread = threadCreate(OutputThread, this, 16 * 1024, 0x18, -2, false);
		if (_thread == nullptr) {
			LOGE("Cannot start the audio output thread, sound will be disabled");
			return;
		}

		_valid = true;
		LOGI("Audio device initialized: NDSP channel {}, mixing at {} Hz stereo in {} blocks of {} frames",
			Channel, _mixFrequency, BlockCount, BlockFrames);
	}

	NdspAudioDevice::~NdspAudioDevice()
	{
		shutdownDecodeThread();

		if (_thread != nullptr) {
			_threadShouldQuit.store(true, std::memory_order_relaxed);
			LightEvent_Signal(&_blockDone);
			threadJoin(_thread, U64_MAX);
			threadFree(_thread);
			_thread = nullptr;
		}
		if (_ndspInitialized) {
			ndspSetCallback(nullptr, nullptr);
			ndspChnWaveBufClear(Channel);
			ndspExit();
			_ndspInitialized = false;
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
		if (_blocks != nullptr) {
			linearFree(_blocks);
			_blocks = nullptr;
		}
	}

	void NdspAudioDevice::FrameCallback(void* arg)
	{
		// Runs on libctru's NDSP thread once per DSP frame; only the wake-up happens here, the mixing belongs
		// to the output thread so the driver's own thread stays lean
		NdspAudioDevice* device = static_cast<NdspAudioDevice*>(arg);
		LightEvent_Signal(&device->_blockDone);
	}

	void NdspAudioDevice::OutputThread(void* arg)
	{
		NdspAudioDevice* device = static_cast<NdspAudioDevice*>(arg);
		while (!device->_threadShouldQuit.load(std::memory_order_relaxed)) {
			// A timeout rather than an unbounded wait, so a DSP that stopped delivering frames (sleep mode) cannot
			// keep the thread from noticing a shutdown request
			LightEvent_WaitTimeout(&device->_blockDone, 20 * 1000 * 1000);
			if (device->_threadShouldQuit.load(std::memory_order_relaxed)) {
				break;
			}
			// Every buffer the DSP is done with gets the next block of the mix and goes back into the ring
			for (std::int32_t i = 0; i < BlockCount; i++) {
				if (device->_waveBufs[i].status == NDSP_WBUF_DONE) {
					device->FillBlock(i);
				}
			}
		}
	}

	void NdspAudioDevice::FillBlock(std::int32_t index)
	{
		std::int16_t* block = _blocks + std::size_t(index) * BlockFrames * ChannelCount;
		const std::size_t blockBytes = std::size_t(BlockFrames) * ChannelCount * sizeof(std::int16_t);
		if (_suspended.load(std::memory_order_relaxed)) {
			std::memset(block, 0, blockBytes);
		} else {
			LockGuard lock(&_lock);
			MixInto(block, BlockFrames);
		}
		// The DSP reads the linear heap without seeing the ARM11's data cache
		DSP_FlushDataCache(block, blockBytes);
		ndspChnWaveBufAdd(Channel, &_waveBufs[index]);
	}

	const char* NdspAudioDevice::name() const
	{
		return "NDSP";
	}

	std::int32_t NdspAudioDevice::nativeFrequency()
	{
		// The rate the sources are mixed at rather than the DSP's own: the module decoder sizes itself by
		// this, and rendering it above the mixing rate would only be resampled straight back down
		return _mixFrequency;
	}

	void NdspAudioDevice::setMixingFrequency(std::int32_t frequency)
	{
		// The channel is resampled by the DSP whatever the rate, so anything the option can ask for is fine;
		// the bounds only keep a corrupt setting from asking the DSP for something absurd
		if (frequency < 8000 || frequency > 48000) {
			if (frequency != 0) {
				LOGW("Cannot mix at {} Hz, keeping {} Hz", frequency, _mixFrequency);
			}
			return;
		}

		LockGuard lock(&_lock);
		if (_mixFrequency != frequency) {
			_mixFrequency = frequency;
			if (_ndspInitialized) {
				// The blocks already queued were mixed at the old rate and play a little fast or slow for a
				// few dozen milliseconds, which is not worth draining the ring for
				ndspChnSetRate(Channel, float(_mixFrequency));
			}
			LOGI("Mixing sound at {} Hz", _mixFrequency);
		}
	}

	void NdspAudioDevice::updatePlayers()
	{
		// Only the players advance here; the mixing itself happens on the output thread
		AudioDeviceBase::updatePlayers();
	}

	void NdspAudioDevice::suspendDevice()
	{
		_suspended.store(true, std::memory_order_relaxed);
	}

	void NdspAudioDevice::resumeDevice()
	{
		_suspended.store(false, std::memory_order_relaxed);
	}

	bool NdspAudioDevice::isValid() const
	{
		return _valid;
	}

	void NdspAudioDevice::setGain(float gain)
	{
		// Read by the mixer per block; a torn float is not possible on this CPU, so no lock
		_gain = gain;
	}

	void NdspAudioDevice::updateListener(const Vector3f& position, const Vector3f& velocity)
	{
		static_cast<void>(velocity);
		LockGuard lock(&_lock);
		_listenerPos = position;
	}

	std::uint32_t NdspAudioDevice::registerPlayer(IAudioPlayer* player)
	{
		LockGuard lock(&_lock);
		const std::uint32_t sourceId = AudioDeviceBase::registerPlayer(player);
		if (sourceId != UnavailableSource) {
			if (Source* source = GetSource(sourceId)) {
				*source = Source{};
			}
		}
		return sourceId;
	}

	NdspAudioDevice::Source* NdspAudioDevice::GetSource(std::uint32_t sourceId)
	{
		if (sourceId == 0 || sourceId > std::uint32_t(MaxSources)) {
			return nullptr;
		}
		return &_sources[sourceId - 1];
	}

	std::uint32_t NdspAudioDevice::createBuffer(BufferUsage usage)
	{
		LockGuard lock(&_lock);
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

	void NdspAudioDevice::ReleaseBuffer(Buffer& buffer)
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

	void NdspAudioDevice::deleteBuffer(std::uint32_t bufferId)
	{
		LockGuard lock(&_lock);
		if (bufferId == 0 || bufferId >= std::uint32_t(_bufferCount)) {
			return;
		}
		ReleaseBuffer(_buffers[bufferId]);
		_buffers[bufferId].Used = false;
	}

	bool NdspAudioDevice::uploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency)
	{
		LockGuard lock(&_lock);
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
		// re-centered here, 16-bit is already native-endian
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

	void NdspAudioDevice::setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		LockGuard lock(&_lock);
		if (Source* source = GetSource(sourceId)) {
			source->BufferId = bufferId;
			source->Cursor = 0;
		}
	}

	void NdspAudioDevice::setSourceGain(std::uint32_t sourceId, float gain)
	{
		LockGuard lock(&_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Gain = gain;
		}
	}

	void NdspAudioDevice::setSourcePitch(std::uint32_t sourceId, float pitch)
	{
		LockGuard lock(&_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Pitch = (pitch > 0.0f ? pitch : 1.0f);
		}
	}

	void NdspAudioDevice::setSourceLooping(std::uint32_t sourceId, bool looping)
	{
		LockGuard lock(&_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Looping = looping;
		}
	}

	void NdspAudioDevice::setSourceRelative(std::uint32_t sourceId, bool relative)
	{
		LockGuard lock(&_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Relative = relative;
		}
	}

	void NdspAudioDevice::setSourcePosition(std::uint32_t sourceId, const Vector3f& position)
	{
		LockGuard lock(&_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Position = position;
		}
	}

	void NdspAudioDevice::setSourceLowPass(std::uint32_t sourceId, float value)
	{
		// No per-voice DSP in this mixer (the channel's own filter would apply to the whole mix)
		static_cast<void>(sourceId);
		static_cast<void>(value);
	}

	std::int32_t NdspAudioDevice::sourceSampleOffset(std::uint32_t sourceId)
	{
		LockGuard lock(&_lock);
		Source* source = GetSource(sourceId);
		return (source != nullptr ? std::int32_t(source->Cursor >> 32) : 0);
	}

	void NdspAudioDevice::setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset)
	{
		LockGuard lock(&_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Cursor = std::int64_t(offset > 0 ? offset : 0) << 32;
		}
	}

	void NdspAudioDevice::playSource(std::uint32_t sourceId)
	{
		LockGuard lock(&_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Playing = true;
			source->Paused = false;
		}
	}

	void NdspAudioDevice::pauseSource(std::uint32_t sourceId)
	{
		LockGuard lock(&_lock);
		if (Source* source = GetSource(sourceId)) {
			source->Paused = true;
		}
	}

	void NdspAudioDevice::stopSource(std::uint32_t sourceId)
	{
		LockGuard lock(&_lock);
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

	bool NdspAudioDevice::isSourcePlaying(std::uint32_t sourceId)
	{
		LockGuard lock(&_lock);
		const Source* source = GetSource(sourceId);
		return (source != nullptr && source->Playing && !source->Paused);
	}

	void NdspAudioDevice::queueBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		LockGuard lock(&_lock);
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

	std::int32_t NdspAudioDevice::numProcessedBuffers(std::uint32_t sourceId)
	{
		LockGuard lock(&_lock);
		const Source* source = GetSource(sourceId);
		return (source != nullptr ? source->ProcessedCount : 0);
	}

	void NdspAudioDevice::unqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds)
	{
		LockGuard lock(&_lock);
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

	NdspAudioDevice::Buffer* NdspAudioDevice::GetActiveBuffer(Source& source)
	{
		const std::uint32_t bufferId = (source.QueueCount > 0 ? source.Queue[0] : source.BufferId);
		if (bufferId == 0 || bufferId >= std::uint32_t(_bufferCount)) {
			return nullptr;
		}
		Buffer& buffer = _buffers[bufferId];
		return (buffer.Used && buffer.Samples != nullptr && buffer.FrameCount > 0 ? &buffer : nullptr);
	}

	DEATH_ALWAYS_INLINE void NdspAudioDevice::ReadFrame(const Buffer& buffer, std::int32_t frame, std::int32_t& left, std::int32_t& right)
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

	void NdspAudioDevice::ComputePanning(const Source& source, float& leftGain, float& rightGain) const
	{
		AudioMixer::ComputeStereoGains(source.Relative, source.Position, _listenerPos, source.Gain, _gain, leftGain, rightGain);
	}

	bool NdspAudioDevice::MixSource(Source& source, std::int32_t* output, std::int32_t frames)
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

	void NdspAudioDevice::MixInto(std::int16_t* output, std::int32_t frames)
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
			return;
		}

		std::int32_t* accumulator = _mixBuffer;
		std::memset(accumulator, 0, std::size_t(frames) * ChannelCount * sizeof(std::int32_t));

		for (Source& source : _sources) {
			if (!source.Playing || source.Paused) {
				continue;
			}
			if (!MixSource(source, accumulator, frames)) {
				source.Playing = false;
				source.Cursor = 0;
			}
		}

		// Clamp the wide accumulator into the channel's 16 bits; the DSP resamples the block to its own rate
		const std::int32_t total = frames * ChannelCount;
		for (std::int32_t i = 0; i < total; i++) {
			std::int32_t value = accumulator[i];
			value = (value < -32768 ? -32768 : (value > 32767 ? 32767 : value));
			output[i] = std::int16_t(value);
		}
	}
}

#endif
