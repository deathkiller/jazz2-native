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
		 * Averaging rather than dropping samples, because what makes a sound long enough to need
		 * this is usually a sustained one, and plain decimation folds its high end back down into
		 * the audible range as aliasing.
		 */
		void decimateInterleaved(const void* source, void* destination, std::int32_t frames,
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
		 * WAV keeps 8-bit samples unsigned with silence at 0x80, the sound processor's 8-bit mode is
		 * two's complement with silence at 0 like its 16-bit one. Flipping the top bit of every byte is
		 * exactly that conversion. @p bytes has to be a multiple of 4 and @p block 4-byte aligned, which
		 * is what every block uploaded to sound RAM is anyway.
		 */
		void convertUnsigned8ToSigned8(void* block, std::int32_t bytes)
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
		void sendChannelCommand(std::int32_t channel, const aica_channel_t& channelData)
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
		// The streaming driver keeps its ring buffers in sound RAM as well, and has to be told how
		// large they may get up front
		if (snd_stream_init_ex(2, StreamBufferSize) < 0) {
			LOGE("snd_stream_init_ex() failed, streamed audio will be unavailable");
		}

		_initialized = true;

		std::uint32_t sourceIds[MaxSources];
		for (std::int32_t i = 0; i < MaxSources; i++) {
			sourceIds[i] = std::uint32_t(i + 1);
		}
		setSourcePool(arrayView(sourceIds, MaxSources));

		LOGI("--- AICA audio device info ---");
		LOGI("Sound Memory: {} KB available", snd_mem_available() / 1024);
		LOGI("Sources: {} ({} of them can stream)", MaxSources, MaxStreams);
	}

	AicaAudioDevice::~AicaAudioDevice()
	{
		LOGD("Disposing AICA audio device...");

		// Shut down the decoding thread first, so it doesn't touch any readers afterwards
		shutdownDecodeThread();

		if (_initialized) {
			for (std::int32_t i = 0; i < MaxSources; i++) {
				stopSample(i);
				releaseStream(i);
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

	bool AicaAudioDevice::isValid() const
	{
		return _initialized;
	}

	const char* AicaAudioDevice::name() const
	{
		return "AICA";
	}

	std::int32_t AicaAudioDevice::nativeFrequency()
	{
		// Every channel is resampled by the hardware, so decoding at the output rate would buy
		// nothing and only cost the SH4 more time in the module mixer
		return PreferredFrequency;
	}

	void AicaAudioDevice::setGain(float gain)
	{
		_gain = gain;

		// There is no master volume in the hardware, it is folded into each channel instead
		for (std::int32_t i = 0; i < MaxSources; i++) {
			applyVolume(i);
		}
	}

	void AicaAudioDevice::updateListener(const Vector3f& position, const Vector3f& velocity)
	{
		_listenerPos = position;

		// Moving the listener changes the panning and attenuation of every source that is not
		// relative to it, which the mix here has to be told about explicitly
		for (std::int32_t i = 0; i < MaxSources; i++) {
			if (!_sources[i].relative) {
				applyVolume(i);
			}
		}
	}

	std::int32_t AicaAudioDevice::sourceForId(std::uint32_t sourceId)
	{
		if (sourceId == 0 || sourceId > std::uint32_t(MaxSources)) {
			return -1;
		}
		return std::int32_t(sourceId) - 1;
	}

	AicaAudioDevice::Buffer* AicaAudioDevice::bufferForId(std::uint32_t bufferId)
	{
		if (bufferId == 0 || bufferId > _buffers.size()) {
			return nullptr;
		}
		Buffer& buffer = _buffers[bufferId - 1];
		return (buffer.used ? &buffer : nullptr);
	}

	std::uint32_t AicaAudioDevice::registerPlayer(IAudioPlayer* player)
	{
		const std::uint32_t sourceId = AudioDeviceBase::registerPlayer(player);

		// A source handed to a new player starts from a clean slate, inheriting anything from the
		// previous owner would decide whether it loops or streams behind the player's back
		const std::int32_t index = sourceForId(sourceId);
		if (index >= 0) {
			_sources[index] = Source();
		}
		return sourceId;
	}

	void AicaAudioDevice::updatePlayers()
	{
		// The stream driver is not interrupt driven, its ring buffers are topped up from here. This
		// runs before the players so that a buffer the driver finished with this frame is already
		// counted as processed by the time the stream tries to reclaim it.
		for (std::int32_t i = 0; i < MaxSources; i++) {
			Source& source = _sources[i];
			if (source.streamHandle >= 0 && source.started && !source.paused) {
				snd_stream_poll(source.streamHandle);
			}
		}

		AudioDeviceBase::updatePlayers();
	}

	std::uint32_t AicaAudioDevice::createBuffer(BufferUsage usage)
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

	void AicaAudioDevice::deleteBuffer(std::uint32_t bufferId)
	{
		Buffer* buffer = bufferForId(bufferId);
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

	bool AicaAudioDevice::uploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency)
	{
		Buffer* buffer = bufferForId(bufferId);
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
			// The stream driver reads from main memory and transfers the block itself, so it only has
			// to be aligned well enough for its fast path
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

		// A fully loaded sound goes into the sound processor's own memory and stays there. An AICA
		// channel is mono, so a stereo sample is de-interleaved into one block per channel.

		// Whatever this buffer held before is gone: a long sample being replaced by a short one has to
		// give its main memory back, the sound RAM blocks are dealt with further down
		if (buffer->longSample) {
			std::free(buffer->data);
			buffer->data = nullptr;
			buffer->capacity = 0;
			buffer->longSample = false;
		}

		// A channel plays at most 65534 samples from wherever it is pointed at, which a long sound
		// effect can exceed - the sugar rush jingle is 21 seconds at 22 kHz. A sound that would have
		// to be resampled more than once to fit (see MaxSamplesForHalving) is kept in main memory
		// instead and played through a stream handle that is fed from it (see startLongSample), which
		// is what the hardware offers for audio longer than a channel: it sounds whole and at its full
		// rate, for the price of the sample living on the heap rather than in sound RAM. 16-bit
		// samples are handed to the stream driver as they are (interleaved, it splits a stereo pair
		// itself), 8-bit ones are kept here converted to signed the same way as below and widened to
		// 16-bit as they are handed over.
		if (buffer->numSamples > MaxSamplesForHalving && size > 0 && data != nullptr) {
			const std::int32_t requiredCapacity = (size + StreamAlignment - 1) & ~(StreamAlignment - 1);
			std::uint8_t* longData = static_cast<std::uint8_t*>(::memalign(StreamAlignment, requiredCapacity));
			if (longData != nullptr) {
				// The driver rounds its last request up to a few bytes past the end, so the padding is
				// silence in whichever encoding the samples are in
				std::memcpy(longData, data, size);
				std::memset(longData + size, (is16Bit ? 0x00 : 0x80), requiredCapacity - size);
				if (!is16Bit) {
					convertUnsigned8ToSigned8(longData, requiredCapacity);
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

		// Otherwise the sample rate is halved until the sound fits a channel: the hardware resamples
		// every channel on playback anyway, so it still plays to its end at its proper pitch and only
		// loses some high end. Only a sound the memory above could not be had for is halved more than
		// once, which loses a lot more.
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
				decimateInterleaved(data, decimated.Pointer, buffer->numSamples,
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

		// Reloading the same buffer from something with a different channel count has to reallocate
		// even when the existing blocks are large enough - going from mono to stereo needs a second
		// block that is not there yet, and going the other way would leak the one no longer used
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
			// 8-bit PCM arrives unsigned, as WAV keeps it, and the hardware plays 8-bit samples as signed
			// (see convertUnsigned8ToSigned8). Uploaded as they are, every sample is off by half the
			// range and each zero crossing becomes a full-scale step - the whole effect turns into loud
			// crackling, not a subtle artifact - so they are re-centred on the way in. It is done on the
			// temporary block the upload is made from, the caller's data is never touched.
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
					// The padding up to the next word is filled with unsigned silence, so it converts
					// to signed silence with the rest instead of whatever followed the caller's block
					std::memset(converted.Pointer, 0x80, requiredCapacity);
					std::memcpy(converted.Pointer, data, size);
					convertUnsigned8ToSigned8(converted.Pointer, requiredCapacity);
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
					convertUnsigned8ToSigned8(left, requiredCapacity);
					convertUnsigned8ToSigned8(right, requiredCapacity);
				}

				spu_memload(buffer->spuAddress[0], left, requiredCapacity);
				spu_memload(buffer->spuAddress[1], right, requiredCapacity);
			}
		}

		buffer->size = size;
		return true;
	}

	void AicaAudioDevice::computeVolume(std::int32_t index, bool isStereo, std::int32_t& volume, std::int32_t& pan)
	{
		const Source& source = _sources[index];

		float level = _gain * source.gain;
		float panning = 0.0f;

		// A stereo buffer is played straight to the output and ignores its position, which is what
		// OpenAL does as well
		if (!isStereo) {
			// The player hands over a position in the same physical frame the listener lives in (see
			// IAudioPlayer::getAdjustedPosition), so it only has to be made relative to the listener
			Vector3f relative = source.position;
			if (!source.relative) {
				relative -= Vector3f(_listenerPos.X * LengthToPhysical,
					_listenerPos.Y * -LengthToPhysical, _listenerPos.Z * -LengthToPhysical);
			}

			const float distance = relative.Length();

			// AL_LINEAR_DISTANCE_CLAMPED with a rolloff factor of 1, matching the OpenAL backend so a
			// sound is equally loud on both
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

	void AicaAudioDevice::applyVolume(std::int32_t index)
	{
		Source& source = _sources[index];
		if (!source.started) {
			return;
		}

		if (source.streaming || source.longSample) {
			if (source.streamHandle < 0) {
				return;
			}
			const Buffer* buffer = (source.longSample ? bufferForId(source.attachedBufferId)
				: (source.numQueued > 0 ? bufferForId(source.queuedBufferIds[0]) : nullptr));
			const bool isStereo = (buffer != nullptr && buffer->numChannels == 2);

			std::int32_t volume, pan;
			computeVolume(index, isStereo, volume, pan);
			snd_stream_volume(source.streamHandle, volume);
			if (!isStereo) {
				snd_stream_pan(source.streamHandle, pan, pan);
			}
			return;
		}

		const Buffer* buffer = bufferForId(source.attachedBufferId);
		const bool isStereo = (buffer != nullptr && buffer->numChannels == 2);

		std::int32_t volume, pan;
		computeVolume(index, isStereo, volume, pan);

		aica_channel_t channelData {};
		channelData.cmd = AICA_CH_CMD_UPDATE | AICA_CH_UPDATE_SET_VOL | AICA_CH_UPDATE_SET_PAN;
		channelData.vol = volume;

		if (!isStereo) {
			if (source.channels[0] >= 0) {
				channelData.pan = pan;
				sendChannelCommand(source.channels[0], channelData);
			}
		} else if (source.channels[0] >= 0 && source.channels[1] >= 0) {
			// The two halves of a stereo sample stay hard left and hard right
			snd_sh4_to_aica_stop();
			channelData.pan = 0;
			sendChannelCommand(source.channels[0], channelData);
			channelData.pan = MaxVolume;
			sendChannelCommand(source.channels[1], channelData);
			snd_sh4_to_aica_start();
		}
	}

	void AicaAudioDevice::startSample(std::int32_t index, std::int32_t sampleOffset)
	{
		Source& source = _sources[index];
		const Buffer* buffer = bufferForId(source.attachedBufferId);
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
					stopSample(index);
					return;
				}
			}
		}

		source.started = true;
		std::int32_t volume, pan;
		computeVolume(index, isStereo, volume, pan);

		// Resuming after a pause is expressed by moving the start address forward: the driver always
		// rewinds a channel to position zero when it is keyed on, so there is nothing to seek with
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

		// The end of a one-shot sample is predicted rather than read back: the hardware's key-on bit
		// stays set until something keys the channel off, so it cannot say when the sample ran out
		source.sampleStartOffset = sampleOffset;
		source.sampleEndAt = (source.looping ? 0 : timer_ms_gettime64()
			+ std::uint64_t(channelData.length) * 1000 / std::uint64_t(channelData.freq) + SampleEndMarginMs);

		if (!isStereo) {
			channelData.pan = pan;
			sendChannelCommand(source.channels[0], channelData);
		} else {
			// Both halves have to be keyed on together or the channels drift apart
			snd_sh4_to_aica_stop();
			channelData.pan = 0;
			sendChannelCommand(source.channels[0], channelData);
			channelData.base = buffer->spuAddress[1] + byteOffset;
			channelData.pan = MaxVolume;
			sendChannelCommand(source.channels[1], channelData);
			snd_sh4_to_aica_start();
		}
	}

	void AicaAudioDevice::stopSample(std::int32_t index)
	{
		Source& source = _sources[index];

		aica_channel_t channelData {};
		channelData.cmd = AICA_CH_CMD_STOP;

		for (std::int32_t& channel : source.channels) {
			if (channel >= 0) {
				sendChannelCommand(channel, channelData);
				snd_sfx_chn_free(channel);
				channel = -1;
			}
		}
		source.sampleEndAt = 0;
	}

	void AicaAudioDevice::startLongSample(std::int32_t index, std::int32_t sampleOffset)
	{
		Source& source = _sources[index];
		const Buffer* buffer = bufferForId(source.attachedBufferId);
		if (buffer == nullptr || !buffer->longSample || buffer->data == nullptr || buffer->frequency <= 0) {
			return;
		}

		if (sampleOffset < 0 || sampleOffset >= buffer->numSamples) {
			sampleOffset = 0;
		}

		// The handle is taken for the duration of one playback and given back when it ends, so the
		// few there are (see MaxStreams) are shared between the music and every long effect
		if (source.streamHandle < 0) {
			source.streamHandle = snd_stream_alloc(streamCallback, StreamBufferSize);
			if (source.streamHandle < 0) {
				LOGW("All {} stream handles are in use, a long sound will not be heard", MaxStreams);
				return;
			}
		}

		// The stream plays 16-bit PCM, so an 8-bit sample is widened on the way (see fillStream) through
		// a staging block that belongs to the handle
		if (buffer->bytesPerSample == 1 && source.streamHandle < MaxStreams && _longSampleStaging[source.streamHandle] == nullptr) {
			_longSampleStaging[source.streamHandle] = static_cast<std::uint8_t*>(::memalign(StreamAlignment, LongSampleStagingSize));
			if (_longSampleStaging[source.streamHandle] == nullptr) {
				LOGW("Cannot allocate {} bytes to widen a long 8-bit sound, it will not be heard", LongSampleStagingSize);
				releaseStream(index);
				return;
			}
		}

		source.longSample = true;
		source.longSamplePos = sampleOffset;
		source.longSampleStagingHalf = 0;
		source.longSampleDrainedAt = 0;
		source.started = true;

		// Starting prefills the ring buffer through the callback, which serves it from the sample. A
		// stream is resampled at the rate it is started with, so the pitch is folded in here and
		// cannot follow later changes.
		std::int32_t frequency = std::int32_t(buffer->frequency * source.pitch + 0.5f);
		if (frequency < 1) {
			frequency = 1;
		}
		snd_stream_start(source.streamHandle, std::uint32_t(frequency), (buffer->numChannels == 2 ? 1 : 0));
		applyVolume(index);
	}

	void AicaAudioDevice::stopLongSample(std::int32_t index)
	{
		Source& source = _sources[index];
		if (!source.longSample) {
			return;
		}

		releaseStream(index);
		source.longSample = false;
		source.longSamplePos = 0;
		source.longSampleDrainedAt = 0;
	}

	void AicaAudioDevice::releaseStream(std::int32_t index)
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

	void* AicaAudioDevice::fillStream(std::int32_t index, std::int32_t bytesRequested, std::int32_t& bytesProvided)
	{
		Source& source = _sources[index];
		bytesProvided = 0;

		if (source.longSample) {
			// Served straight out of the sample in main memory, a looping one starts over at its end
			Buffer* buffer = bufferForId(source.attachedBufferId);
			if (buffer == nullptr || !buffer->longSample || buffer->data == nullptr) {
				return nullptr;
			}
			const std::int32_t frameSize = buffer->numChannels * buffer->bytesPerSample;
			std::int32_t offset = source.longSamplePos * frameSize;
			if (offset >= buffer->size) {
				if (!source.looping) {
					// Everything has been handed over, what is left is the ring buffer playing out
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

			// 8-bit samples are widened into the staging block of the handle, which holds two halves
			// used in turn so the one the previous request may still be read from is left alone
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

		// Everything before numProcessed is waiting to be reclaimed by the stream, the buffer being
		// served is the first one after that
		while (source.numProcessed < source.numQueued) {
			Buffer* buffer = bufferForId(source.queuedBufferIds[source.numProcessed]);
			if (buffer == nullptr || buffer->size <= source.headOffset) {
				// Exhausted or unusable, hand it back and move on to the next one
				source.numProcessed++;
				source.headOffset = 0;
				continue;
			}

			// One call never straddles two buffers - the driver takes whatever it is given and asks
			// again, so there is no reason to stitch them together into a copy
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

	void* AicaAudioDevice::streamCallback(int handle, int bytesRequested, int* bytesProvided)
	{
		*bytesProvided = 0;
		if (_current == nullptr) {
			return nullptr;
		}

		for (std::int32_t i = 0; i < MaxSources; i++) {
			if (_current->_sources[i].streamHandle == handle) {
				std::int32_t provided = 0;
				void* result = _current->fillStream(i, bytesRequested, provided);
				*bytesProvided = int(provided);
				return result;
			}
		}
		return nullptr;
	}

	void AicaAudioDevice::setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		const std::int32_t index = sourceForId(sourceId);
		if (index < 0) {
			return;
		}

		Source& source = _sources[index];
		source.attachedBufferId = bufferId;

		if (bufferId == 0) {
			// Detaching is how a player releases a source, so everything about it is reset here
			stopSample(index);
			stopLongSample(index);
			releaseStream(index);
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

	void AicaAudioDevice::setSourceGain(std::uint32_t sourceId, float gain)
	{
		const std::int32_t index = sourceForId(sourceId);
		if (index >= 0) {
			_sources[index].gain = gain;
			applyVolume(index);
		}
	}

	void AicaAudioDevice::setSourcePitch(std::uint32_t sourceId, float pitch)
	{
		const std::int32_t index = sourceForId(sourceId);
		if (index < 0) {
			return;
		}

		Source& source = _sources[index];
		source.pitch = pitch;

		// A stream is resampled at the rate it was started with and the driver has no way to change
		// it afterwards, so only sample playback follows the pitch
		if (source.started && !source.streaming && source.channels[0] >= 0) {
			const Buffer* buffer = bufferForId(source.attachedBufferId);
			if (buffer != nullptr && buffer->frequency > 0) {
				aica_channel_t channelData {};
				channelData.cmd = AICA_CH_CMD_UPDATE | AICA_CH_UPDATE_SET_FREQ;
				channelData.freq = std::int32_t(buffer->frequency * pitch + 0.5f);
				if (channelData.freq < 1) {
					channelData.freq = 1;
				}

				if (source.channels[1] < 0) {
					sendChannelCommand(source.channels[0], channelData);
				} else {
					snd_sh4_to_aica_stop();
					sendChannelCommand(source.channels[0], channelData);
					sendChannelCommand(source.channels[1], channelData);
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

	void AicaAudioDevice::setSourceLooping(std::uint32_t sourceId, bool looping)
	{
		const std::int32_t index = sourceForId(sourceId);
		if (index >= 0) {
			// Taken into account when the channel is keyed on, the loop flag is part of the start
			// command and cannot be changed while a sample is sounding
			_sources[index].looping = looping;
		}
	}

	void AicaAudioDevice::setSourceRelative(std::uint32_t sourceId, bool relative)
	{
		const std::int32_t index = sourceForId(sourceId);
		if (index >= 0) {
			_sources[index].relative = relative;
			applyVolume(index);
		}
	}

	void AicaAudioDevice::setSourcePosition(std::uint32_t sourceId, const Vector3f& position)
	{
		const std::int32_t index = sourceForId(sourceId);
		if (index >= 0) {
			_sources[index].position = position;
			applyVolume(index);
		}
	}

	void AicaAudioDevice::setSourceLowPass(std::uint32_t sourceId, float value)
	{
		// Each AICA channel does have a low-pass filter, but the driver switches it off and offers no
		// command to reach it, so sounds are not muffled underwater
	}

	std::int32_t AicaAudioDevice::sourceSampleOffset(std::uint32_t sourceId)
	{
		const std::int32_t index = sourceForId(sourceId);
		if (index < 0) {
			return 0;
		}

		const Source& source = _sources[index];
		if (source.streaming || !source.started) {
			return 0;
		}
		if (source.longSample) {
			// What has been handed to the driver, which runs a ring buffer ahead of what is heard
			return source.longSamplePos;
		}
		if (source.channels[0] < 0) {
			return 0;
		}
		// The hardware counts from where the channel was pointed at, not from the start of the sample
		return source.sampleStartOffset + snd_get_pos(source.channels[0]);
	}

	void AicaAudioDevice::setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset)
	{
		const std::int32_t index = sourceForId(sourceId);
		if (index < 0) {
			return;
		}

		Source& source = _sources[index];
		if (source.streaming) {
			return;
		}

		// A channel always starts from the beginning of what it is pointed at, so seeking means
		// keying it on again further into the sample - and a long sample is restarted the same way
		if (source.started) {
			const Buffer* buffer = bufferForId(source.attachedBufferId);
			if (buffer != nullptr && buffer->longSample) {
				stopLongSample(index);
				startLongSample(index, offset);
			} else {
				stopSample(index);
				startSample(index, offset);
			}
		} else {
			source.pausedOffset = offset;
		}
	}

	void AicaAudioDevice::playSource(std::uint32_t sourceId)
	{
		const std::int32_t index = sourceForId(sourceId);
		if (index < 0) {
			return;
		}

		Source& source = _sources[index];
		const bool wasPaused = source.paused;
		source.paused = false;

		if (source.attachedBufferId != 0) {
			const Buffer* buffer = bufferForId(source.attachedBufferId);
			const bool isLongSample = (buffer != nullptr && buffer->longSample);
			if (source.started && !wasPaused) {
				// Already sounding, restart it from the beginning like alSourcePlay() would
				stopSample(index);
				stopLongSample(index);
			}
			if (isLongSample) {
				if (_inBlockingOperation) {
					// Nothing feeds a stream until the operation is over, so it waits for it instead
					source.pausedOffset = (wasPaused ? source.pausedOffset : 0);
					source.stoppedForBlocking = true;
					return;
				}
				startLongSample(index, wasPaused ? source.pausedOffset : 0);
			} else {
				startSample(index, wasPaused ? source.pausedOffset : 0);
			}
			source.pausedOffset = 0;
			return;
		}

		// A streaming source, which cannot start before it has something queued
		source.streaming = true;
		if (_inBlockingOperation) {
			source.stoppedForBlocking = true;
			return;
		}
		startStream(index);
	}

	void AicaAudioDevice::startStream(std::int32_t index)
	{
		Source& source = _sources[index];
		if (source.streamHandle < 0 || source.numQueued == 0) {
			return;
		}
		if (source.started) {
			return;
		}

		const Buffer* buffer = bufferForId(source.queuedBufferIds[0]);
		if (buffer == nullptr || buffer->frequency <= 0) {
			return;
		}

		source.started = true;
		// Prefills the ring buffer through the callback, which is served from the queue above
		snd_stream_start(source.streamHandle, buffer->frequency, buffer->numChannels == 2 ? 1 : 0);
		applyVolume(index);
	}

	void AicaAudioDevice::pauseSource(std::uint32_t sourceId)
	{
		const std::int32_t index = sourceForId(sourceId);
		if (index < 0) {
			return;
		}

		Source& source = _sources[index];
		if (source.stoppedForBlocking) {
			// Already stopped for the operation in progress, so it only has to stay stopped afterwards -
			// the position to resume from is where the stop left it
			source.stoppedForBlocking = false;
			source.paused = true;
			return;
		}
		if (!source.started || source.paused) {
			return;
		}
		source.paused = true;

		if (source.streaming) {
			// The driver has no pause, so the channels are stopped and the ring buffer is refilled
			// from the queue when playback resumes - the queue itself is not touched
			snd_stream_stop(source.streamHandle);
			source.started = false;
		} else if (source.longSample) {
			// The driver is ahead of what has been heard by up to a ring buffer, so resuming from
			// where the feeding got to would skip that much - it is taken back instead, and a little
			// of the sound is heard twice rather than lost
			// The ring holds StreamBufferSize bytes of 16-bit samples per channel
			const std::int32_t ringFrames = StreamBufferSize / 2;
			source.pausedOffset = (source.longSamplePos > ringFrames ? source.longSamplePos - ringFrames : 0);
			stopLongSample(index);
			source.started = false;
		} else {
			// Remember where the sample got to, so it can be keyed on again from there - counted from
			// the start of the sample, whichever offset the channel was pointed at this time
			source.pausedOffset = source.sampleStartOffset + (source.channels[0] >= 0 ? snd_get_pos(source.channels[0]) : 0);
			stopSample(index);
			source.started = false;
		}
	}

	void AicaAudioDevice::stopSource(std::uint32_t sourceId)
	{
		const std::int32_t index = sourceForId(sourceId);
		if (index < 0) {
			return;
		}

		Source& source = _sources[index];
		if (source.streaming) {
			if (source.started) {
				snd_stream_stop(source.streamHandle);
			}
			// A stopped source hands every buffer it still holds back to its owner, which reclaims
			// them through numProcessedBuffers() and unqueueBuffers() right after this
			source.numProcessed = source.numQueued;
			source.headOffset = 0;
		} else if (source.longSample) {
			stopLongSample(index);
		} else {
			stopSample(index);
		}

		source.started = false;
		source.paused = false;
		source.pausedOffset = 0;
		source.stoppedForBlocking = false;
	}

	bool AicaAudioDevice::isSourcePlaying(std::uint32_t sourceId)
	{
		const std::int32_t index = sourceForId(sourceId);
		if (index < 0) {
			return false;
		}

		const Source& source = _sources[index];
		if (source.stoppedForBlocking) {
			// Merely waiting for the load to end, the players must not take it for finished
			return true;
		}
		if (!source.started || source.paused) {
			return false;
		}

		if (source.streaming) {
			// The driver plays silence rather than stopping when it runs dry, so the stream is
			// "playing" for as long as it has been started - AudioStream decides when it is over
			return true;
		}
		if (source.longSample) {
			// Over once the last of the sample has been handed to the driver AND the ring buffer it
			// keeps ahead of the output has had time to play out - a whole ring buffer's worth,
			// which errs on the side of a sound that is reported a little longer than it is heard
			if (source.longSampleDrainedAt == 0) {
				return true;
			}
			const Buffer* buffer = bufferForId(source.attachedBufferId);
			std::uint64_t ringMs = 0;
			if (buffer != nullptr && buffer->frequency > 0) {
				// The ring holds StreamBufferSize bytes of 16-bit samples per channel
				ringMs = std::uint64_t(StreamBufferSize / 2) * 1000 / std::uint64_t(buffer->frequency);
			}
			return (timer_ms_gettime64() - source.longSampleDrainedAt < ringMs);
		}
		// A one-shot sample is over when its predicted end has passed. The key-on bit the driver
		// exposes is asked as well, but only as confirmation of an earlier stop: on the console it is a
		// control bit that stays set until the channel is keyed off, whatever the sample did, so a
		// source that waited for it was never given back and the pool ran dry after a few seconds
		if (source.sampleEndAt != 0 && timer_ms_gettime64() >= source.sampleEndAt) {
			return false;
		}
		return (source.channels[0] >= 0 && snd_is_playing(source.channels[0]));
	}

	void AicaAudioDevice::queueBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		const std::int32_t index = sourceForId(sourceId);
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
			source.streamHandle = snd_stream_alloc(streamCallback, StreamBufferSize);
			if (source.streamHandle < 0) {
				LOGW("All {} stream handles are in use, a stream will not be heard", MaxStreams);
				return;
			}
		}

		source.queuedBufferIds[source.numQueued] = bufferId;
		source.numQueued++;
	}

	std::int32_t AicaAudioDevice::numProcessedBuffers(std::uint32_t sourceId)
	{
		const std::int32_t index = sourceForId(sourceId);
		return (index >= 0 ? _sources[index].numProcessed : 0);
	}

	void AicaAudioDevice::unqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds)
	{
		const std::int32_t index = sourceForId(sourceId);
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

	void AicaAudioDevice::onBlockingOperationBegan()
	{
		// The stream driver's ring is topped up from updatePlayers(), which the caller is about to stop
		// calling for far longer than the ring holds - 16 KB is a third of a second of 22 kHz stereo, a
		// level load off the disc takes several. The channel keeps looping the ring meanwhile, so the
		// last fragment of the music repeats for the whole load: a grinding sound on every level change
		// and on the way back to the menu (whose episode scan opens the same window). Stopped here, the
		// load is silent instead and every stream picks up where it was once it is over. Sounds on
		// channels play out of sound RAM on their own and are left alone.
		_inBlockingOperation = true;

		for (std::int32_t i = 0; i < MaxSources; i++) {
			Source& source = _sources[i];
			if (source.streamHandle < 0 || !source.started || source.paused) {
				continue;
			}
			if (source.streaming) {
				// Same as a pause: the queue is kept and the ring is refilled from it when it restarts
				snd_stream_stop(source.streamHandle);
				source.started = false;
				source.stoppedForBlocking = true;
			} else if (source.longSample) {
				// Same as a pause of a long sample, see pauseSource()
				const std::int32_t ringFrames = StreamBufferSize / 2;
				source.pausedOffset = (source.longSamplePos > ringFrames ? source.longSamplePos - ringFrames : 0);
				stopLongSample(i);
				source.started = false;
				source.stoppedForBlocking = true;
			}
		}
	}

	void AicaAudioDevice::onBlockingOperationEnded()
	{
		_inBlockingOperation = false;

		for (std::int32_t i = 0; i < MaxSources; i++) {
			Source& source = _sources[i];
			if (!source.stoppedForBlocking) {
				continue;
			}
			source.stoppedForBlocking = false;

			if (source.streaming) {
				startStream(i);
			} else if (source.attachedBufferId != 0) {
				const Buffer* buffer = bufferForId(source.attachedBufferId);
				if (buffer != nullptr && buffer->longSample) {
					startLongSample(i, source.pausedOffset);
					source.pausedOffset = 0;
				}
			}
		}
	}

	void AicaAudioDevice::suspendDevice()
	{
		// Deliberately nothing. The obvious implementation, spu_disable()/spu_enable(), holds the ARM
		// core in reset - which makes it restart its driver from the top when it is let go again, with
		// re-initialized queues and every channel forgotten, leaving the state tracked here describing
		// a sound processor that no longer exists. There is nothing to suspend to on this console
		// anyway: the engine only calls this when a window loses focus, which cannot happen here.
	}

	void AicaAudioDevice::resumeDevice()
	{
	}
}

#endif
