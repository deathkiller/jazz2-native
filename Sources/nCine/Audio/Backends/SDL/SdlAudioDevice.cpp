#include "SdlAudioDevice.h"

#if defined(WITH_SDLAUDIO)

#include "../../AudioMixerCommon.h"
#include "../../IAudioPlayer.h"
#include "../../../../Main.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(WITH_SDL3)
#	include <SDL3/SDL.h>
#else
#	include <SDL.h>
#endif

namespace nCine
{
	SdlAudioDevice::SdlAudioDevice()
		: _valid(false), _suspended(false), _subsystemInitialized(false), _outputFrequency(44100), _deviceId(0),
			_block(nullptr), _mixBuffer(nullptr), _buffers(nullptr), _bufferCount(0), _bufferCapacity(0)
	{
		if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
			LOGE("Cannot initialize SDL audio ({}), sound will be disabled", SDL_GetError());
			return;
		}
		_subsystemInitialized = true;

		SDL_AudioSpec want{};
		want.freq = _outputFrequency;
		want.format = AUDIO_S16SYS;
		want.channels = ChannelCount;
		// SDL is told the block size the mixer works in, so its own buffering lines up with ours
		want.samples = BlockFrames;
		want.callback = nullptr;

		SDL_AudioSpec have{};
		// The rate and the buffer size may be adjusted (the mixer follows whatever comes back), but a
		// changed format or channel count would need conversion the mixer does not do, so those are refused
		_deviceId = SDL_OpenAudioDevice(nullptr, 0, &want, &have,
			SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
		if (_deviceId == 0) {
			LOGE("Cannot open an SDL audio device ({}), sound will be disabled", SDL_GetError());
			return;
		}
		_outputFrequency = have.freq;

		_block = static_cast<std::int16_t*>(std::malloc(std::size_t(BlockFrames) * ChannelCount * sizeof(std::int16_t)));
		_mixBuffer = static_cast<std::int32_t*>(std::malloc(std::size_t(BlockFrames) * ChannelCount * sizeof(std::int32_t)));
		_buffers = static_cast<Buffer*>(std::malloc(std::size_t(InitialBufferCapacity) * sizeof(Buffer)));
		if (_block == nullptr || _mixBuffer == nullptr || _buffers == nullptr) {
			LOGE("Cannot allocate the audio mixing buffers, sound will be disabled");
			return;
		}
		_bufferCapacity = InitialBufferCapacity;

		// Buffer id 0 is reserved as "no buffer"
		::new (&_buffers[0]) Buffer{};
		_bufferCount = 1;

		std::uint32_t sourceIds[MaxSources];
		for (std::int32_t i = 0; i < MaxSources; i++) {
			sourceIds[i] = std::uint32_t(i + 1);
		}
		setSourcePool(arrayView(sourceIds, MaxSources));

		SDL_PauseAudioDevice(_deviceId, 0);

		_valid = true;
		LOGI("Audio device initialized: SDL, mixing {} Hz stereo in blocks of {} frames", _outputFrequency, BlockFrames);
	}

	SdlAudioDevice::~SdlAudioDevice()
	{
		shutdownDecodeThread();

		if (_deviceId != 0) {
			SDL_CloseAudioDevice(_deviceId);
			_deviceId = 0;
		}
		// Only what this instance brought up is taken down again - the subsystem is reference-counted,
		// and a quit that no init of ours matches would count against whoever else initialized it
		if (_subsystemInitialized) {
			SDL_QuitSubSystem(SDL_INIT_AUDIO);
			_subsystemInitialized = false;
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
		std::free(_block);
		_block = nullptr;
	}

	const char* SdlAudioDevice::name() const
	{
		return "SDL";
	}

	std::int32_t SdlAudioDevice::nativeFrequency()
	{
		return _outputFrequency;
	}

	void SdlAudioDevice::updatePlayers()
	{
		AudioDeviceBase::updatePlayers();

		if (_valid && !_suspended) {
			FillQueue();
		}
	}

	void SdlAudioDevice::FillQueue()
	{
		const std::int32_t blockBytes = BlockFrames * ChannelCount * std::int32_t(sizeof(std::int16_t));
		const std::uint32_t targetBytes = std::uint32_t(TargetQueuedFrames) * ChannelCount * sizeof(std::int16_t);

		// Bounded by the target rather than by a block count: a frame that fell behind refills the whole
		// queue, and one that did not costs a single mix of silence at most
		while (SDL_GetQueuedAudioSize(_deviceId) < targetBytes) {
			MixInto(_block, BlockFrames);
			if (SDL_QueueAudio(_deviceId, _block, std::uint32_t(blockBytes)) != 0) {
				LOGW("Cannot queue audio ({})", SDL_GetError());
				break;
			}
		}
	}

	void SdlAudioDevice::suspendDevice()
	{
		if (_deviceId != 0) {
			SDL_PauseAudioDevice(_deviceId, 1);
			// What is already queued would otherwise play on resume, up to a fifth of a second late
			SDL_ClearQueuedAudio(_deviceId);
		}
		_suspended = true;
	}

	void SdlAudioDevice::resumeDevice()
	{
		if (_deviceId != 0) {
			SDL_PauseAudioDevice(_deviceId, 0);
		}
		_suspended = false;
	}

	bool SdlAudioDevice::isValid() const
	{
		return _valid;
	}

	void SdlAudioDevice::setGain(float gain)
	{
		// Applied while mixing, so the device itself always plays at unity
		_gain = gain;
	}

	void SdlAudioDevice::updateListener(const Vector3f& position, const Vector3f& velocity)
	{
		static_cast<void>(velocity);
		_listenerPos = position;
	}

	std::uint32_t SdlAudioDevice::registerPlayer(IAudioPlayer* player)
	{
		const std::uint32_t sourceId = AudioDeviceBase::registerPlayer(player);
		if (sourceId != UnavailableSource) {
			if (Source* source = GetSource(sourceId)) {
				*source = Source{};
			}
		}
		return sourceId;
	}

	SdlAudioDevice::Source* SdlAudioDevice::GetSource(std::uint32_t sourceId)
	{
		if (sourceId == 0 || sourceId > std::uint32_t(MaxSources)) {
			return nullptr;
		}
		return &_sources[sourceId - 1];
	}

	std::uint32_t SdlAudioDevice::createBuffer(BufferUsage usage)
	{
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

	void SdlAudioDevice::ReleaseBuffer(Buffer& buffer)
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

	void SdlAudioDevice::deleteBuffer(std::uint32_t bufferId)
	{
		if (bufferId == 0 || bufferId >= std::uint32_t(_bufferCount)) {
			return;
		}
		ReleaseBuffer(_buffers[bufferId]);
		_buffers[bufferId].Used = false;
	}

	bool SdlAudioDevice::uploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency)
	{
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
		buffer.Frequency = (frequency > 0 ? frequency : _outputFrequency);
		buffer.FrameCount = frameCount;
		return true;
	}

	void SdlAudioDevice::setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->BufferId = bufferId;
			source->Cursor = 0;
		}
	}

	void SdlAudioDevice::setSourceGain(std::uint32_t sourceId, float gain)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Gain = gain;
		}
	}

	void SdlAudioDevice::setSourcePitch(std::uint32_t sourceId, float pitch)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Pitch = (pitch > 0.0f ? pitch : 1.0f);
		}
	}

	void SdlAudioDevice::setSourceLooping(std::uint32_t sourceId, bool looping)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Looping = looping;
		}
	}

	void SdlAudioDevice::setSourceRelative(std::uint32_t sourceId, bool relative)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Relative = relative;
		}
	}

	void SdlAudioDevice::setSourcePosition(std::uint32_t sourceId, const Vector3f& position)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Position = position;
		}
	}

	void SdlAudioDevice::setSourceLowPass(std::uint32_t sourceId, float value)
	{
		// No per-voice DSP in this mixer (see the N64 backend for why the branch is not worth it)
		static_cast<void>(sourceId);
		static_cast<void>(value);
	}

	std::int32_t SdlAudioDevice::sourceSampleOffset(std::uint32_t sourceId)
	{
		Source* source = GetSource(sourceId);
		return (source != nullptr ? std::int32_t(source->Cursor >> 32) : 0);
	}

	void SdlAudioDevice::setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Cursor = std::int64_t(offset > 0 ? offset : 0) << 32;
		}
	}

	void SdlAudioDevice::playSource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Playing = true;
			source->Paused = false;
		}
	}

	void SdlAudioDevice::pauseSource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Paused = true;
		}
	}

	void SdlAudioDevice::stopSource(std::uint32_t sourceId)
	{
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

	bool SdlAudioDevice::isSourcePlaying(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr && source->Playing && !source->Paused);
	}

	void SdlAudioDevice::queueBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
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

	std::int32_t SdlAudioDevice::numProcessedBuffers(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr ? source->ProcessedCount : 0);
	}

	void SdlAudioDevice::unqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds)
	{
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

	SdlAudioDevice::Buffer* SdlAudioDevice::GetActiveBuffer(Source& source)
	{
		const std::uint32_t bufferId = (source.QueueCount > 0 ? source.Queue[0] : source.BufferId);
		if (bufferId == 0 || bufferId >= std::uint32_t(_bufferCount)) {
			return nullptr;
		}
		Buffer& buffer = _buffers[bufferId];
		return (buffer.Used && buffer.Samples != nullptr && buffer.FrameCount > 0 ? &buffer : nullptr);
	}

	DEATH_ALWAYS_INLINE void SdlAudioDevice::ReadFrame(const Buffer& buffer, std::int32_t frame, std::int32_t& left, std::int32_t& right)
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

	void SdlAudioDevice::ComputePanning(const Source& source, float& leftGain, float& rightGain) const
	{
		AudioMixer::ComputeStereoGains(source.Relative, source.Position, _listenerPos, source.Gain, _gain, leftGain, rightGain);
	}

	bool SdlAudioDevice::MixSource(Source& source, std::int32_t* output, std::int32_t frames)
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

		std::int64_t step = std::int64_t((double(buffer->Frequency) / double(_outputFrequency)) * double(source.Pitch) * 4294967296.0);
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
					step = std::int64_t((double(buffer->Frequency) / double(_outputFrequency)) * double(source.Pitch) * 4294967296.0);
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

	void SdlAudioDevice::MixInto(std::int16_t* output, std::int32_t frames)
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

		// Clamp the wide accumulator into the device's 16 bits
		const std::int32_t total = frames * ChannelCount;
		for (std::int32_t i = 0; i < total; i++) {
			std::int32_t value = accumulator[i];
			value = (value < -32768 ? -32768 : (value > 32767 ? 32767 : value));
			output[i] = std::int16_t(value);
		}
	}
}

#endif
