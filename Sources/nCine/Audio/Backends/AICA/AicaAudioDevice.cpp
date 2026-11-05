#include "AicaAudioDevice.h"

#if defined(WITH_AICA)

#include "../../../../Main.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include <malloc.h>

#include <arch/timer.h>
#include <dc/spu.h>
#include <dc/sound/sound.h>
#include <dc/sound/sfxmgr.h>
#include <dc/sound/stream.h>
#include <dc/sound/aica_comm.h>

namespace nCine
{
	namespace
	{
		/** @brief Alignment the stream driver wants for the block it is handed */
		constexpr std::int32_t StreamAlignment = 32;
		/** @brief Panning value that places a channel in the middle */
		constexpr std::int32_t CenterPan = 128;

		/**
		 * @brief Halves the sample rate of an interleaved block @p factor times, by averaging
		 *
		 * Averaging instead of dropping samples, which would alias the high end.
		 */
		void DecimateInterleaved(const void* source, void* destination, std::int32_t frames,
			std::int32_t numChannels, std::int32_t bytesPerSample, std::int32_t factor)
		{
			const std::int32_t outFrames = frames / factor;

			if (bytesPerSample == 2) {
				const std::int16_t* src = static_cast<const std::int16_t*>(source);
				std::int16_t* dst = static_cast<std::int16_t*>(destination);
				for (std::int32_t f = 0; f < outFrames; f++) {
					for (std::int32_t c = 0; c < numChannels; c++) {
						std::int32_t accumulator = 0;
						for (std::int32_t k = 0; k < factor; k++) {
							const std::int32_t frame = f * factor + k;
							accumulator += src[(frame < frames ? frame : frames - 1) * numChannels + c];
						}
						dst[f * numChannels + c] = std::int16_t(accumulator / factor);
					}
				}
			} else {
				// 8-bit PCM is unsigned, so it averages around the 128 midpoint on its own
				const std::uint8_t* src = static_cast<const std::uint8_t*>(source);
				std::uint8_t* dst = static_cast<std::uint8_t*>(destination);
				for (std::int32_t f = 0; f < outFrames; f++) {
					for (std::int32_t c = 0; c < numChannels; c++) {
						std::int32_t accumulator = 0;
						for (std::int32_t k = 0; k < factor; k++) {
							const std::int32_t frame = f * factor + k;
							accumulator += src[(frame < frames ? frame : frames - 1) * numChannels + c];
						}
						dst[f * numChannels + c] = std::uint8_t(accumulator / factor);
					}
				}
			}
		}

		/**
		 * @brief Turns a block of unsigned 8-bit PCM into the signed 8-bit PCM the AICA plays, in place
		 *
		 * Flips the top bit of every byte. @p bytes must be a multiple of 4 and @p block 4-byte aligned.
		 */
		void ConvertUnsigned8ToSigned8(void* block, std::int32_t bytes)
		{
			std::uint32_t* words = static_cast<std::uint32_t*>(block);
			for (std::int32_t i = 0, n = bytes / 4; i < n; i++) {
				words[i] ^= 0x80808080u;
			}
		}

		/** @brief Frees a `malloc()`ed block when it goes out of scope, whichever way the caller left */
		struct ScopedAlloc
		{
			void* Pointer;

			explicit ScopedAlloc(void* pointer = nullptr) : Pointer(pointer) {}
			~ScopedAlloc() { std::free(Pointer); }

			ScopedAlloc(const ScopedAlloc&) = delete;
			ScopedAlloc& operator=(const ScopedAlloc&) = delete;
		};

		/** @brief Sends one channel command to the AICA */
		void SendChannelCommand(std::int32_t channel, const aica_channel_t& channelData)
		{
			AICA_CMDSTR_CHANNEL(tmp, cmd, chan);

			cmd->cmd = AICA_CMD_CHAN;
			cmd->timestamp = 0;
			cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
			cmd->cmd_id = channel;
			*chan = channelData;
			snd_sh4_to_aica(tmp, cmd->size);
		}
	}

	AicaAudioDevice* AicaAudioDevice::_current = nullptr;

	AicaAudioDevice::AicaAudioDevice()
		: _initialized(false), _inBlockingOperation(false), _longSampleStaging {}
	{
		LOGD("Initializing AICA audio device...");

		_current = this;

		// Uploads the driver the ARM core runs and sets up the sound RAM allocator
		if (snd_init() < 0) {
			LOGE("snd_init() failed, the game will be silent");
			return;
		}
		if (snd_stream_init_ex(2, StreamBufferSize) < 0) {
			LOGE("snd_stream_init_ex() failed, streamed audio will be unavailable");
		}

		_initialized = true;

		std::uint32_t sourceIds[MaxSources];
		for (std::int32_t i = 0; i < MaxSources; i++) {
			sourceIds[i] = std::uint32_t(i + 1);
		}
		SetSourcePool(arrayView(sourceIds, MaxSources));

		LOGI("--- AICA audio device info ---");
		LOGI("Sound Memory: {} KB available", snd_mem_available() / 1024);
		LOGI("Sources: {} ({} of them can stream)", MaxSources, MaxStreams);
	}

	AicaAudioDevice::~AicaAudioDevice()
	{
		LOGD("Disposing AICA audio device...");

		// Shut down the decoding thread first, so it doesn't touch any readers afterwards
		ShutdownDecodeThread();

		if (_initialized) {
			for (std::int32_t i = 0; i < MaxSources; i++) {
				StopSample(i);
				ReleaseStream(i);
			}
			snd_stream_shutdown();
			snd_shutdown();
			_initialized = false;
		}

		for (Buffer& buffer : _buffers) {
			for (std::uint32_t address : buffer.spuAddress) {
				if (address != 0) {
					snd_mem_free(address);
				}
			}
			std::free(buffer.data);
		}
		_buffers.clear();

		for (std::uint8_t*& staging : _longSampleStaging) {
			std::free(staging);
			staging = nullptr;
		}

		_current = nullptr;
	}

	bool AicaAudioDevice::IsValid() const
	{
		return _initialized;
	}

	const char* AicaAudioDevice::GetName() const
	{
		return "AICA";
	}

	std::int32_t AicaAudioDevice::GetNativeFrequency()
	{
		// The hardware resamples every channel, a higher rate would only cost the SH4 more time
		return PreferredFrequency;
	}

	void AicaAudioDevice::SetGain(float gain)
	{
		_gain = gain;

		// There is no master volume in the hardware, it is folded into each channel instead
		for (std::int32_t i = 0; i < MaxSources; i++) {
			ApplyVolume(i);
		}
	}

	void AicaAudioDevice::UpdateListener(const Vector3f& position, const Vector3f& velocity)
	{
		_listenerPos = position;

		for (std::int32_t i = 0; i < MaxSources; i++) {
			if (!_sources[i].relative) {
				ApplyVolume(i);
			}
		}
	}

	std::int32_t AicaAudioDevice::GetSourceIndex(std::uint32_t sourceId)
	{
		if (sourceId == 0 || sourceId > std::uint32_t(MaxSources)) {
			return -1;
		}
		return std::int32_t(sourceId) - 1;
	}

	AicaAudioDevice::Buffer* AicaAudioDevice::FindBuffer(std::uint32_t bufferId)
	{
		if (bufferId == 0 || bufferId > _buffers.size()) {
			return nullptr;
		}
		Buffer& buffer = _buffers[bufferId - 1];
		return (buffer.used ? &buffer : nullptr);
	}

	std::uint32_t AicaAudioDevice::RegisterPlayer(IAudioPlayer* player)
	{
		const std::uint32_t sourceId = AudioDeviceBase::RegisterPlayer(player);

		// Start from a clean slate, so nothing (looping, streaming) is inherited from the previous owner
		const std::int32_t index = GetSourceIndex(sourceId);
		if (index >= 0) {
			_sources[index] = Source();
		}
		return sourceId;
	}

	void AicaAudioDevice::UpdatePlayers()
	{
		// The stream driver is polled, not interrupt driven. Runs before the players, so buffers
		// finished this frame are already counted as processed.
		for (std::int32_t i = 0; i < MaxSources; i++) {
			Source& source = _sources[i];
			if (source.streamHandle >= 0 && source.started && !source.paused) {
				snd_stream_poll(source.streamHandle);
			}
		}

		AudioDeviceBase::UpdatePlayers();
	}

	std::uint32_t AicaAudioDevice::CreateBuffer(BufferUsage usage)
	{
		for (std::size_t i = 0; i < _buffers.size(); i++) {
			if (!_buffers[i].used) {
				_buffers[i].used = true;
				_buffers[i].usage = usage;
				_buffers[i].size = 0;
				return std::uint32_t(i + 1);
			}
		}

		_buffers.emplace_back();
		_buffers.back().used = true;
		_buffers.back().usage = usage;
		return std::uint32_t(_buffers.size());
	}

	void AicaAudioDevice::DeleteBuffer(std::uint32_t bufferId)
	{
		Buffer* buffer = FindBuffer(bufferId);
		if (buffer == nullptr) {
			return;
		}

		for (std::uint32_t& address : buffer->spuAddress) {
			if (address != 0) {
				snd_mem_free(address);
				address = 0;
			}
		}
		std::free(buffer->data);
		*buffer = Buffer();
	}

	bool AicaAudioDevice::UploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency)
	{
		Buffer* buffer = FindBuffer(bufferId);
		if (buffer == nullptr || size < 0) {
			return false;
		}

		const bool isStereo = (format == BufferFormat::Stereo8 || format == BufferFormat::Stereo16);
		const bool is16Bit = (format == BufferFormat::Mono16 || format == BufferFormat::Stereo16);
		buffer->numChannels = (isStereo ? 2 : 1);
		buffer->bytesPerSample = (is16Bit ? 2 : 1);
		buffer->frequency = frequency;
		buffer->numSamples = size / (buffer->numChannels * buffer->bytesPerSample);

		if (buffer->usage == BufferUsage::Streaming) {
			// The stream driver reads from main memory, aligned for its fast path
			const std::int32_t requiredCapacity = (size + StreamAlignment - 1) & ~(StreamAlignment - 1);
			if (requiredCapacity > buffer->capacity) {
				std::free(buffer->data);
				buffer->data = static_cast<std::uint8_t*>(::memalign(StreamAlignment, requiredCapacity));
				buffer->capacity = (buffer->data != nullptr ? requiredCapacity : 0);
				if (buffer->data == nullptr) {
					buffer->size = 0;
					LOGE("Cannot allocate {} bytes for a streaming audio buffer", requiredCapacity);
					return false;
				}
			}
			if (size > 0 && data != nullptr) {
				std::memcpy(buffer->data, data, size);
			}
			buffer->size = size;
			return true;
		}

		// A long sample being replaced gives its main memory back, sound RAM is handled below
		if (buffer->longSample) {
			std::free(buffer->data);
			buffer->data = nullptr;
			buffer->capacity = 0;
			buffer->longSample = false;
		}

		// Too long for a channel even at half rate, so keep it in main memory and stream it (see StartLongSample())
		if (buffer->numSamples > MaxSamplesForHalving && size > 0 && data != nullptr) {
			const std::int32_t requiredCapacity = (size + StreamAlignment - 1) & ~(StreamAlignment - 1);
			std::uint8_t* longData = static_cast<std::uint8_t*>(::memalign(StreamAlignment, requiredCapacity));
			if (longData != nullptr) {
				// The driver reads a few bytes past the end, so pad with silence
				std::memcpy(longData, data, size);
				std::memset(longData + size, (is16Bit ? 0x00 : 0x80), requiredCapacity - size);
				if (!is16Bit) {
					ConvertUnsigned8ToSigned8(longData, requiredCapacity);
				}

				for (std::uint32_t& address : buffer->spuAddress) {
					if (address != 0) {
						snd_mem_free(address);
						address = 0;
					}
				}
				buffer->spuCapacity = 0;
				std::free(buffer->data);
				buffer->data = longData;
				buffer->capacity = requiredCapacity;
				buffer->size = size;
				buffer->longSample = true;
				LOGD("Audio buffer holds {} samples, more than the {} an AICA channel can address, it will be streamed from main memory",
					buffer->numSamples, MaxSamplesPerChannel);
				return true;
			}

			LOGW("Cannot allocate {} bytes to keep a long audio buffer in main memory, resampling it instead", requiredCapacity);
		}

		// Otherwise halve the sample rate until it fits a channel, which only loses some high end
		ScopedAlloc decimated;
		if (buffer->numSamples > MaxSamplesPerChannel) {
			std::int32_t factor = 2;
			while (buffer->numSamples / factor > MaxSamplesPerChannel) {
				factor *= 2;
			}

			const std::int32_t outFrames = buffer->numSamples / factor;
			const std::int32_t outSize = outFrames * buffer->numChannels * buffer->bytesPerSample;

			if (size > 0 && data != nullptr) {
				decimated.Pointer = ::memalign(32, (outSize + 31) & ~31);
				if (decimated.Pointer == nullptr) {
					LOGE("Cannot allocate {} bytes to resample an oversized audio buffer", outSize);
					buffer->size = 0;
					return false;
				}
				DecimateInterleaved(data, decimated.Pointer, buffer->numSamples,
					buffer->numChannels, buffer->bytesPerSample, factor);
				data = decimated.Pointer;
			}

			LOGW("Audio buffer holds {} samples, more than the {} an AICA channel can address, "
				"resampling it to {} Hz ({} samples)", buffer->numSamples, MaxSamplesPerChannel,
				frequency / factor, outFrames);

			buffer->numSamples = outFrames;
			buffer->frequency = frequency / factor;
			size = outSize;
		}

		const std::int32_t bytesPerChannel = buffer->numSamples * buffer->bytesPerSample;
		// spu_memload() moves whole 32-bit words
		const std::int32_t requiredCapacity = (bytesPerChannel + 3) & ~3;

		// A changed channel count reallocates even if the blocks are large enough (missing or leaked second block)
		const bool channelCountChanged = (buffer->spuCapacity > 0 &&
			(buffer->spuAddress[1] != 0) != isStereo);

		if (requiredCapacity > buffer->spuCapacity || channelCountChanged) {
			for (std::uint32_t& address : buffer->spuAddress) {
				if (address != 0) {
					snd_mem_free(address);
					address = 0;
				}
			}
			buffer->spuCapacity = 0;

			if (requiredCapacity > 0) {
				for (std::int32_t i = 0; i < buffer->numChannels; i++) {
					buffer->spuAddress[i] = snd_mem_malloc(requiredCapacity);
					if (buffer->spuAddress[i] == 0) {
						LOGE("Cannot allocate {} bytes of sound memory ({} KB left)", requiredCapacity, snd_mem_available() / 1024);
						for (std::uint32_t& address : buffer->spuAddress) {
							if (address != 0) {
								snd_mem_free(address);
								address = 0;
							}
						}
						buffer->size = 0;
						return false;
					}
				}
				buffer->spuCapacity = requiredCapacity;
			}
		}

		if (size > 0 && data != nullptr) {
			// 8-bit PCM arrives unsigned but the hardware plays it signed, so convert a copy (unconverted it crackles loudly)
			if (!isStereo) {
				if (is16Bit) {
					spu_memload(buffer->spuAddress[0], data, requiredCapacity);
				} else {
					ScopedAlloc converted(::memalign(32, (requiredCapacity + 31) & ~31));
					if (converted.Pointer == nullptr) {
						LOGE("Cannot allocate {} bytes to convert an 8-bit audio buffer", requiredCapacity);
						buffer->size = 0;
						return false;
					}
					// Pad with unsigned silence, so it converts to signed silence
					std::memset(converted.Pointer, 0x80, requiredCapacity);
					std::memcpy(converted.Pointer, data, size);
					ConvertUnsigned8ToSigned8(converted.Pointer, requiredCapacity);
					spu_memload(buffer->spuAddress[0], converted.Pointer, requiredCapacity);
				}
			} else {
				// The two halves are built in one temporary block and uploaded separately
				ScopedAlloc separated(::memalign(32, requiredCapacity * 2));
				if (separated.Pointer == nullptr) {
					LOGE("Cannot allocate {} bytes to de-interleave a stereo audio buffer", requiredCapacity * 2);
					buffer->size = 0;
					return false;
				}

				std::uint8_t* left = static_cast<std::uint8_t*>(separated.Pointer);
				std::uint8_t* right = left + requiredCapacity;
				if (is16Bit) {
					snd_pcm16_split((std::uint32_t*)data, (std::uint32_t*)left, (std::uint32_t*)right, size);
				} else {
					snd_pcm8_split((std::uint32_t*)data, (std::uint32_t*)left, (std::uint32_t*)right, size);
					ConvertUnsigned8ToSigned8(left, requiredCapacity);
					ConvertUnsigned8ToSigned8(right, requiredCapacity);
				}

				spu_memload(buffer->spuAddress[0], left, requiredCapacity);
				spu_memload(buffer->spuAddress[1], right, requiredCapacity);
			}
		}

		buffer->size = size;
		return true;
	}

	void AicaAudioDevice::ComputeVolume(std::int32_t index, bool isStereo, std::int32_t& volume, std::int32_t& pan)
	{
		const Source& source = _sources[index];

		float level = _gain * source.gain;
		float panning = 0.0f;

		// A stereo buffer ignores its position, like in OpenAL
		if (!isStereo) {
			// Already in the listener's physical frame (see IAudioPlayer::GetAdjustedPosition)
			Vector3f relative = source.position;
			if (!source.relative) {
				relative -= Vector3f(_listenerPos.X * LengthToPhysical,
					_listenerPos.Y * -LengthToPhysical, _listenerPos.Z * -LengthToPhysical);
			}

			const float distance = relative.Length();

			// AL_LINEAR_DISTANCE_CLAMPED with rolloff 1, to match the OpenAL backend
			if (distance > ReferenceDistance) {
				const float clamped = (distance < MaxDistance ? distance : MaxDistance);
				level *= 1.0f - (clamped - ReferenceDistance) / (MaxDistance - ReferenceDistance);
			}

			if (distance > 0.0001f) {
				panning = relative.X / distance;
				panning = (panning < -1.0f ? -1.0f : (panning > 1.0f ? 1.0f : panning));
			}
		}

		std::int32_t scaled = std::int32_t(level * MaxVolume + 0.5f);
		volume = (scaled < 0 ? 0 : (scaled > MaxVolume ? MaxVolume : scaled));
		pan = CenterPan + std::int32_t(panning * CenterPan);
		pan = (pan < 0 ? 0 : (pan > MaxVolume ? MaxVolume : pan));
	}

	void AicaAudioDevice::ApplyVolume(std::int32_t index)
	{
		Source& source = _sources[index];
		if (!source.started) {
			return;
		}

		if (source.streaming || source.longSample) {
			if (source.streamHandle < 0) {
				return;
			}
			const Buffer* buffer = (source.longSample ? FindBuffer(source.attachedBufferId)
				: (source.numQueued > 0 ? FindBuffer(source.queuedBufferIds[0]) : nullptr));
			const bool isStereo = (buffer != nullptr && buffer->numChannels == 2);

			std::int32_t volume, pan;
			ComputeVolume(index, isStereo, volume, pan);
			snd_stream_volume(source.streamHandle, volume);
			if (!isStereo) {
				snd_stream_pan(source.streamHandle, pan, pan);
			}
			return;
		}

		const Buffer* buffer = FindBuffer(source.attachedBufferId);
		const bool isStereo = (buffer != nullptr && buffer->numChannels == 2);

		std::int32_t volume, pan;
		ComputeVolume(index, isStereo, volume, pan);

		aica_channel_t channelData {};
		channelData.cmd = AICA_CH_CMD_UPDATE | AICA_CH_UPDATE_SET_VOL | AICA_CH_UPDATE_SET_PAN;
		channelData.vol = volume;

		if (!isStereo) {
			if (source.channels[0] >= 0) {
				channelData.pan = pan;
				SendChannelCommand(source.channels[0], channelData);
			}
		} else if (source.channels[0] >= 0 && source.channels[1] >= 0) {
			// The two halves of a stereo sample stay hard left and hard right
			snd_sh4_to_aica_stop();
			channelData.pan = 0;
			SendChannelCommand(source.channels[0], channelData);
			channelData.pan = MaxVolume;
			SendChannelCommand(source.channels[1], channelData);
			snd_sh4_to_aica_start();
		}
	}

	void AicaAudioDevice::StartSample(std::int32_t index, std::int32_t sampleOffset)
	{
		Source& source = _sources[index];
		const Buffer* buffer = FindBuffer(source.attachedBufferId);
		if (buffer == nullptr || buffer->numSamples <= 0 || buffer->spuAddress[0] == 0) {
			return;
		}

		const bool isStereo = (buffer->numChannels == 2);
		if (isStereo && buffer->spuAddress[1] == 0) {
			return;
		}

		if (sampleOffset < 0 || sampleOffset >= buffer->numSamples) {
			sampleOffset = 0;
		}

		// Claim the channels this source needs, one per buffer channel
		for (std::int32_t i = 0, n = (isStereo ? 2 : 1); i < n; i++) {
			if (source.channels[i] < 0) {
				source.channels[i] = snd_sfx_chn_alloc();
				if (source.channels[i] < 0) {
					LOGW("No AICA channel is free, a sound will not be heard");
					StopSample(index);
					return;
				}
			}
		}

		source.started = true;
		std::int32_t volume, pan;
		ComputeVolume(index, isStereo, volume, pan);

		// The driver always keys on at position zero, so an offset moves the start address instead
		const std::int32_t byteOffset = sampleOffset * buffer->bytesPerSample;

		aica_channel_t channelData {};
		channelData.cmd = AICA_CH_CMD_START;
		channelData.base = buffer->spuAddress[0] + byteOffset;
		channelData.type = (buffer->bytesPerSample == 2 ? AICA_SM_16BIT : AICA_SM_8BIT);
		channelData.length = buffer->numSamples - sampleOffset;
		channelData.loop = (source.looping ? 1 : 0);
		channelData.loopstart = 0;
		channelData.loopend = buffer->numSamples - sampleOffset;
		channelData.freq = std::int32_t(buffer->frequency * source.pitch + 0.5f);
		channelData.vol = volume;

		if (channelData.freq < 1) {
			channelData.freq = 1;
		}

		// The end is predicted, the key-on bit stays set after a one-shot sample ends
		source.sampleStartOffset = sampleOffset;
		source.sampleEndAt = (source.looping ? 0 : timer_ms_gettime64()
			+ std::uint64_t(channelData.length) * 1000 / std::uint64_t(channelData.freq) + SampleEndMarginMs);

		if (!isStereo) {
			channelData.pan = pan;
			SendChannelCommand(source.channels[0], channelData);
		} else {
			// Both halves have to be keyed on together or the channels drift apart
			snd_sh4_to_aica_stop();
			channelData.pan = 0;
			SendChannelCommand(source.channels[0], channelData);
			channelData.base = buffer->spuAddress[1] + byteOffset;
			channelData.pan = MaxVolume;
			SendChannelCommand(source.channels[1], channelData);
			snd_sh4_to_aica_start();
		}
	}

	void AicaAudioDevice::StopSample(std::int32_t index)
	{
		Source& source = _sources[index];

		aica_channel_t channelData {};
		channelData.cmd = AICA_CH_CMD_STOP;

		for (std::int32_t& channel : source.channels) {
			if (channel >= 0) {
				SendChannelCommand(channel, channelData);
				snd_sfx_chn_free(channel);
				channel = -1;
			}
		}
		source.sampleEndAt = 0;
	}

	void AicaAudioDevice::StartLongSample(std::int32_t index, std::int32_t sampleOffset)
	{
		Source& source = _sources[index];
		const Buffer* buffer = FindBuffer(source.attachedBufferId);
		if (buffer == nullptr || !buffer->longSample || buffer->data == nullptr || buffer->frequency <= 0) {
			return;
		}

		if (sampleOffset < 0 || sampleOffset >= buffer->numSamples) {
			sampleOffset = 0;
		}

		// Held only for one playback, the few handles are shared with the music
		if (source.streamHandle < 0) {
			source.streamHandle = snd_stream_alloc(StreamCallback, StreamBufferSize);
			if (source.streamHandle < 0) {
				LOGW("All {} stream handles are in use, a long sound will not be heard", MaxStreams);
				return;
			}
		}

		// The stream plays 16-bit PCM, 8-bit samples are widened through a per-handle staging block
		if (buffer->bytesPerSample == 1 && source.streamHandle < MaxStreams && _longSampleStaging[source.streamHandle] == nullptr) {
			_longSampleStaging[source.streamHandle] = static_cast<std::uint8_t*>(::memalign(StreamAlignment, LongSampleStagingSize));
			if (_longSampleStaging[source.streamHandle] == nullptr) {
				LOGW("Cannot allocate {} bytes to widen a long 8-bit sound, it will not be heard", LongSampleStagingSize);
				ReleaseStream(index);
				return;
			}
		}

		source.longSample = true;
		source.longSamplePos = sampleOffset;
		source.longSampleStagingHalf = 0;
		source.longSampleDrainedAt = 0;
		source.started = true;

		// A stream's rate is fixed at start, so later pitch changes are ignored
		std::int32_t frequency = std::int32_t(buffer->frequency * source.pitch + 0.5f);
		if (frequency < 1) {
			frequency = 1;
		}
		snd_stream_start(source.streamHandle, std::uint32_t(frequency), (buffer->numChannels == 2 ? 1 : 0));
		ApplyVolume(index);
	}

	void AicaAudioDevice::StopLongSample(std::int32_t index)
	{
		Source& source = _sources[index];
		if (!source.longSample) {
			return;
		}

		ReleaseStream(index);
		source.longSample = false;
		source.longSamplePos = 0;
		source.longSampleDrainedAt = 0;
	}

	void AicaAudioDevice::ReleaseStream(std::int32_t index)
	{
		Source& source = _sources[index];
		if (source.streamHandle < 0) {
			return;
		}

		if (source.started) {
			snd_stream_stop(source.streamHandle);
		}
		snd_stream_destroy(source.streamHandle);
		source.streamHandle = -1;
	}

	void* AicaAudioDevice::FillStream(std::int32_t index, std::int32_t bytesRequested, std::int32_t& bytesProvided)
	{
		Source& source = _sources[index];
		bytesProvided = 0;

		if (source.longSample) {
			Buffer* buffer = FindBuffer(source.attachedBufferId);
			if (buffer == nullptr || !buffer->longSample || buffer->data == nullptr) {
				return nullptr;
			}
			const std::int32_t frameSize = buffer->numChannels * buffer->bytesPerSample;
			std::int32_t offset = source.longSamplePos * frameSize;
			if (offset >= buffer->size) {
				if (!source.looping) {
					// All handed over, only the ring buffer is left to play out
					if (source.longSampleDrainedAt == 0) {
						source.longSampleDrainedAt = timer_ms_gettime64();
					}
					return nullptr;
				}
				source.longSamplePos = 0;
				offset = 0;
			}

			if (buffer->bytesPerSample == 2) {
				std::int32_t available = buffer->size - offset;
				if (available > bytesRequested) {
					available = bytesRequested;
				}
				// Whole frames only, so a stereo pair is never split across two calls
				available -= available % frameSize;
				if (available <= 0) {
					return nullptr;
				}

				source.longSamplePos += available / frameSize;
				bytesProvided = available;
				return buffer->data + offset;
			}

			// Alternate staging halves, the previous one may still be read by DMA
			std::uint8_t* staging = (source.streamHandle >= 0 && source.streamHandle < MaxStreams
				? _longSampleStaging[source.streamHandle] : nullptr);
			if (staging == nullptr) {
				return nullptr;
			}
			const std::int32_t halfSize = LongSampleStagingSize / 2;
			std::int32_t outBytes = (bytesRequested < halfSize ? bytesRequested : halfSize);
			std::int32_t frames = outBytes / (2 * buffer->numChannels);
			const std::int32_t framesLeft = (buffer->size - offset) / frameSize;
			if (frames > framesLeft) {
				frames = framesLeft;
			}
			if (frames <= 0) {
				return nullptr;
			}

			std::int16_t* out = reinterpret_cast<std::int16_t*>(staging + source.longSampleStagingHalf * halfSize);
			const std::int8_t* in = reinterpret_cast<const std::int8_t*>(buffer->data + offset);
			const std::int32_t count = frames * buffer->numChannels;
			for (std::int32_t i = 0; i < count; i++) {
				out[i] = std::int16_t(std::int32_t(in[i]) << 8);
			}
			source.longSampleStagingHalf ^= 1;
			source.longSamplePos += frames;
			bytesProvided = count * 2;
			return out;
		}

		// Buffers before numProcessed wait to be reclaimed, the one being served follows them
		while (source.numProcessed < source.numQueued) {
			Buffer* buffer = FindBuffer(source.queuedBufferIds[source.numProcessed]);
			if (buffer == nullptr || buffer->size <= source.headOffset) {
				// Exhausted or unusable, hand it back and move on to the next one
				source.numProcessed++;
				source.headOffset = 0;
				continue;
			}

			// Never straddles two buffers, the driver just asks again
			std::int32_t available = buffer->size - source.headOffset;
			if (available > bytesRequested) {
				available = bytesRequested;
			}

			std::uint8_t* result = buffer->data + source.headOffset;
			source.headOffset += available;
			if (source.headOffset >= buffer->size) {
				source.numProcessed++;
				source.headOffset = 0;
			}

			bytesProvided = available;
			return result;
		}

		// Nothing queued, the driver fills this block with silence
		return nullptr;
	}

	void* AicaAudioDevice::StreamCallback(int handle, int bytesRequested, int* bytesProvided)
	{
		*bytesProvided = 0;
		if (_current == nullptr) {
			return nullptr;
		}

		for (std::int32_t i = 0; i < MaxSources; i++) {
			if (_current->_sources[i].streamHandle == handle) {
				std::int32_t provided = 0;
				void* result = _current->FillStream(i, bytesRequested, provided);
				*bytesProvided = int(provided);
				return result;
			}
		}
		return nullptr;
	}

	void AicaAudioDevice::SetSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		const std::int32_t index = GetSourceIndex(sourceId);
		if (index < 0) {
			return;
		}

		Source& source = _sources[index];
		source.attachedBufferId = bufferId;

		if (bufferId == 0) {
			// Detaching releases the source, so reset everything
			StopSample(index);
			StopLongSample(index);
			ReleaseStream(index);
			source.numQueued = 0;
			source.numProcessed = 0;
			source.headOffset = 0;
			source.pausedOffset = 0;
			source.streaming = false;
			source.started = false;
			source.paused = false;
			source.stoppedForBlocking = false;
		} else {
			source.streaming = false;
		}
	}

	void AicaAudioDevice::SetSourceGain(std::uint32_t sourceId, float gain)
	{
		const std::int32_t index = GetSourceIndex(sourceId);
		if (index >= 0) {
			_sources[index].gain = gain;
			ApplyVolume(index);
		}
	}

	void AicaAudioDevice::SetSourcePitch(std::uint32_t sourceId, float pitch)
	{
		const std::int32_t index = GetSourceIndex(sourceId);
		if (index < 0) {
			return;
		}

		Source& source = _sources[index];
		source.pitch = pitch;

		// A stream's rate cannot change after start, so only channels follow the pitch
		if (source.started && !source.streaming && source.channels[0] >= 0) {
			const Buffer* buffer = FindBuffer(source.attachedBufferId);
			if (buffer != nullptr && buffer->frequency > 0) {
				aica_channel_t channelData {};
				channelData.cmd = AICA_CH_CMD_UPDATE | AICA_CH_UPDATE_SET_FREQ;
				channelData.freq = std::int32_t(buffer->frequency * pitch + 0.5f);
				if (channelData.freq < 1) {
					channelData.freq = 1;
				}

				if (source.channels[1] < 0) {
					SendChannelCommand(source.channels[0], channelData);
				} else {
					snd_sh4_to_aica_stop();
					SendChannelCommand(source.channels[0], channelData);
					SendChannelCommand(source.channels[1], channelData);
					snd_sh4_to_aica_start();
				}

				// What is left of a one-shot sample now plays out at the new rate
				if (source.sampleEndAt != 0) {
					const std::int32_t keyedOnLength = buffer->numSamples - source.sampleStartOffset;
					const std::int32_t position = snd_get_pos(source.channels[0]);
					const std::int32_t remaining = (position < keyedOnLength ? keyedOnLength - position : 0);
					source.sampleEndAt = timer_ms_gettime64()
						+ std::uint64_t(remaining) * 1000 / std::uint64_t(channelData.freq) + SampleEndMarginMs;
				}
			}
		}
	}

	void AicaAudioDevice::SetSourceLooping(std::uint32_t sourceId, bool looping)
	{
		const std::int32_t index = GetSourceIndex(sourceId);
		if (index >= 0) {
			// Applied on the next key-on, it cannot change while a sample is sounding
			_sources[index].looping = looping;
		}
	}

	void AicaAudioDevice::SetSourceRelative(std::uint32_t sourceId, bool relative)
	{
		const std::int32_t index = GetSourceIndex(sourceId);
		if (index >= 0) {
			_sources[index].relative = relative;
			ApplyVolume(index);
		}
	}

	void AicaAudioDevice::SetSourcePosition(std::uint32_t sourceId, const Vector3f& position)
	{
		const std::int32_t index = GetSourceIndex(sourceId);
		if (index >= 0) {
			_sources[index].position = position;
			ApplyVolume(index);
		}
	}

	void AicaAudioDevice::SetSourceLowPass(std::uint32_t sourceId, float value)
	{
		// The driver doesn't expose the channel low-pass filter, so sounds are not muffled underwater
	}

	std::int32_t AicaAudioDevice::GetSourceSampleOffset(std::uint32_t sourceId)
	{
		const std::int32_t index = GetSourceIndex(sourceId);
		if (index < 0) {
			return 0;
		}

		const Source& source = _sources[index];
		if (source.streaming || !source.started) {
			return 0;
		}
		if (source.longSample) {
			// Position handed to the driver, a ring buffer ahead of what is heard
			return source.longSamplePos;
		}
		if (source.channels[0] < 0) {
			return 0;
		}
		// The hardware counts from the keyed-on offset
		return source.sampleStartOffset + snd_get_pos(source.channels[0]);
	}

	void AicaAudioDevice::SetSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset)
	{
		const std::int32_t index = GetSourceIndex(sourceId);
		if (index < 0) {
			return;
		}

		Source& source = _sources[index];
		if (source.streaming) {
			return;
		}

		// Seeking means restarting further into the sample
		if (source.started) {
			const Buffer* buffer = FindBuffer(source.attachedBufferId);
			if (buffer != nullptr && buffer->longSample) {
				StopLongSample(index);
				StartLongSample(index, offset);
			} else {
				StopSample(index);
				StartSample(index, offset);
			}
		} else {
			source.pausedOffset = offset;
		}
	}

	void AicaAudioDevice::PlaySource(std::uint32_t sourceId)
	{
		const std::int32_t index = GetSourceIndex(sourceId);
		if (index < 0) {
			return;
		}

		Source& source = _sources[index];
		const bool wasPaused = source.paused;
		source.paused = false;

		if (source.attachedBufferId != 0) {
			const Buffer* buffer = FindBuffer(source.attachedBufferId);
			const bool isLongSample = (buffer != nullptr && buffer->longSample);
			if (source.started && !wasPaused) {
				// Already sounding, restart it from the beginning like alSourcePlay() would
				StopSample(index);
				StopLongSample(index);
			}
			if (isLongSample) {
				if (_inBlockingOperation) {
					// Nothing feeds a stream during the operation, start it afterwards
					source.pausedOffset = (wasPaused ? source.pausedOffset : 0);
					source.stoppedForBlocking = true;
					return;
				}
				StartLongSample(index, wasPaused ? source.pausedOffset : 0);
			} else {
				StartSample(index, wasPaused ? source.pausedOffset : 0);
			}
			source.pausedOffset = 0;
			return;
		}

		// Streaming source, starts once something is queued
		source.streaming = true;
		if (_inBlockingOperation) {
			source.stoppedForBlocking = true;
			return;
		}
		StartStream(index);
	}

	void AicaAudioDevice::StartStream(std::int32_t index)
	{
		Source& source = _sources[index];
		if (source.streamHandle < 0 || source.numQueued == 0) {
			return;
		}
		if (source.started) {
			return;
		}

		const Buffer* buffer = FindBuffer(source.queuedBufferIds[0]);
		if (buffer == nullptr || buffer->frequency <= 0) {
			return;
		}

		source.started = true;
		snd_stream_start(source.streamHandle, buffer->frequency, buffer->numChannels == 2 ? 1 : 0);
		ApplyVolume(index);
	}

	void AicaAudioDevice::PauseSource(std::uint32_t sourceId)
	{
		const std::int32_t index = GetSourceIndex(sourceId);
		if (index < 0) {
			return;
		}

		Source& source = _sources[index];
		if (source.stoppedForBlocking) {
			// Already stopped for the blocking operation, just keep it stopped afterwards
			source.stoppedForBlocking = false;
			source.paused = true;
			return;
		}
		if (!source.started || source.paused) {
			return;
		}
		source.paused = true;

		if (source.streaming) {
			// The driver has no pause, stop it and refill the ring from the kept queue on resume
			snd_stream_stop(source.streamHandle);
			source.started = false;
		} else if (source.longSample) {
			// Step back by one ring buffer (16-bit samples), so a little is repeated rather than skipped
			const std::int32_t ringFrames = StreamBufferSize / 2;
			source.pausedOffset = (source.longSamplePos > ringFrames ? source.longSamplePos - ringFrames : 0);
			StopLongSample(index);
			source.started = false;
		} else {
			// Position from the start of the sample, not from the keyed-on offset
			source.pausedOffset = source.sampleStartOffset + (source.channels[0] >= 0 ? snd_get_pos(source.channels[0]) : 0);
			StopSample(index);
			source.started = false;
		}
	}

	void AicaAudioDevice::StopSource(std::uint32_t sourceId)
	{
		const std::int32_t index = GetSourceIndex(sourceId);
		if (index < 0) {
			return;
		}

		Source& source = _sources[index];
		if (source.streaming) {
			if (source.started) {
				snd_stream_stop(source.streamHandle);
			}
			// Mark everything processed, so the owner can unqueue all buffers
			source.numProcessed = source.numQueued;
			source.headOffset = 0;
		} else if (source.longSample) {
			StopLongSample(index);
		} else {
			StopSample(index);
		}

		source.started = false;
		source.paused = false;
		source.pausedOffset = 0;
		source.stoppedForBlocking = false;
	}

	bool AicaAudioDevice::IsSourcePlaying(std::uint32_t sourceId)
	{
		const std::int32_t index = GetSourceIndex(sourceId);
		if (index < 0) {
			return false;
		}

		const Source& source = _sources[index];
		if (source.stoppedForBlocking) {
			// Only waiting for the load to end, must not look finished
			return true;
		}
		if (!source.started || source.paused) {
			return false;
		}

		if (source.streaming) {
			// The driver plays silence when starved, AudioStream decides when it is over
			return true;
		}
		if (source.longSample) {
			// Over once everything was handed over and a whole ring buffer had time to play out
			if (source.longSampleDrainedAt == 0) {
				return true;
			}
			const Buffer* buffer = FindBuffer(source.attachedBufferId);
			std::uint64_t ringMs = 0;
			if (buffer != nullptr && buffer->frequency > 0) {
				ringMs = std::uint64_t(StreamBufferSize / 2) * 1000 / std::uint64_t(buffer->frequency);
			}
			return (timer_ms_gettime64() - source.longSampleDrainedAt < ringMs);
		}
		// Rely on the predicted end, real hardware never clears the key-on bit of a one-shot sample
		if (source.sampleEndAt != 0 && timer_ms_gettime64() >= source.sampleEndAt) {
			return false;
		}
		return (source.channels[0] >= 0 && snd_is_playing(source.channels[0]));
	}

	void AicaAudioDevice::QueueBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		const std::int32_t index = GetSourceIndex(sourceId);
		if (index < 0) {
			return;
		}

		Source& source = _sources[index];
		if (source.numQueued >= MaxQueuedBuffers) {
			LOGW("Streaming queue of source {} is full, dropping a buffer", sourceId);
			return;
		}

		source.streaming = true;
		if (source.streamHandle < 0) {
			source.streamHandle = snd_stream_alloc(StreamCallback, StreamBufferSize);
			if (source.streamHandle < 0) {
				LOGW("All {} stream handles are in use, a stream will not be heard", MaxStreams);
				return;
			}
		}

		source.queuedBufferIds[source.numQueued] = bufferId;
		source.numQueued++;
	}

	std::int32_t AicaAudioDevice::GetProcessedBufferCount(std::uint32_t sourceId)
	{
		const std::int32_t index = GetSourceIndex(sourceId);
		return (index >= 0 ? _sources[index].numProcessed : 0);
	}

	void AicaAudioDevice::UnqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds)
	{
		const std::int32_t index = GetSourceIndex(sourceId);
		if (index < 0) {
			return;
		}

		Source& source = _sources[index];
		if (count > source.numProcessed) {
			count = source.numProcessed;
		}

		for (std::int32_t i = 0; i < count; i++) {
			bufferIds[i] = source.queuedBufferIds[i];
		}

		// Shift the rest of the queue down over the removed entries
		for (std::int32_t i = count; i < source.numQueued; i++) {
			source.queuedBufferIds[i - count] = source.queuedBufferIds[i];
		}
		source.numQueued -= count;
		source.numProcessed -= count;
	}

	void AicaAudioDevice::OnBlockingOperationBegan()
	{
		// UpdatePlayers() won't top up the rings during the load, and a starved ring keeps looping
		// (grinding sound), so stop streams and resume them afterwards. Channels play on their own.
		_inBlockingOperation = true;

		for (std::int32_t i = 0; i < MaxSources; i++) {
			Source& source = _sources[i];
			if (source.streamHandle < 0 || !source.started || source.paused) {
				continue;
			}
			if (source.streaming) {
				// Same as a pause
				snd_stream_stop(source.streamHandle);
				source.started = false;
				source.stoppedForBlocking = true;
			} else if (source.longSample) {
				// Same as a pause of a long sample, see PauseSource()
				const std::int32_t ringFrames = StreamBufferSize / 2;
				source.pausedOffset = (source.longSamplePos > ringFrames ? source.longSamplePos - ringFrames : 0);
				StopLongSample(i);
				source.started = false;
				source.stoppedForBlocking = true;
			}
		}
	}

	void AicaAudioDevice::OnBlockingOperationEnded()
	{
		_inBlockingOperation = false;

		for (std::int32_t i = 0; i < MaxSources; i++) {
			Source& source = _sources[i];
			if (!source.stoppedForBlocking) {
				continue;
			}
			source.stoppedForBlocking = false;

			if (source.streaming) {
				StartStream(i);
			} else if (source.attachedBufferId != 0) {
				const Buffer* buffer = FindBuffer(source.attachedBufferId);
				if (buffer != nullptr && buffer->longSample) {
					StartLongSample(i, source.pausedOffset);
					source.pausedOffset = 0;
				}
			}
		}
	}

	void AicaAudioDevice::SuspendDevice()
	{
		// Deliberately nothing: spu_disable() resets the ARM driver and loses all channel state,
		// and focus loss can't happen on this console anyway
	}

	void AicaAudioDevice::ResumeDevice()
	{
	}
}

#endif
