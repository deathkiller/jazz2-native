#include "AsndAudioDevice.h"

#if defined(WITH_ASND)

#include "../../../../Main.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include <asndlib.h>
#include <gccore.h>
#include <malloc.h>

namespace nCine
{
	namespace
	{
		/** @brief Alignment ASND requires of every buffer it is handed */
		constexpr std::int32_t BufferAlignment = 32;

		std::int32_t ToAsndVoiceFormat(IAudioDevice::BufferFormat format)
		{
			// Samples arrive native-endian (big-endian here), 8-bit PCM is unsigned like AL_FORMAT_MONO8
			switch (format) {
				case IAudioDevice::BufferFormat::Stereo8: return VOICE_STEREO_8BIT_U;
				case IAudioDevice::BufferFormat::Mono16: return VOICE_MONO_16BIT_BE;
				case IAudioDevice::BufferFormat::Stereo16: return VOICE_STEREO_16BIT_BE;
				default: return VOICE_MONO_8BIT_U;
			}
		}

		bool IsStereoFormat(std::int32_t voiceFormat)
		{
			return (voiceFormat == VOICE_STEREO_8BIT_U || voiceFormat == VOICE_STEREO_16BIT_BE);
		}

		// Only keeps a starved streaming voice alive, ASND frees voices without a callback. Runs in
		// the DSP interrupt, the queue is refilled from Pump() instead.
		void StreamVoiceCallback(s32 voice)
		{
		}

		/** @brief libogc's own DSP interrupt callback, which hands each mail to the task it is for */
		DSPCallback libogcDspCallback = nullptr;

		/**
			@brief Lets a DSP interrupt that came without a mail pass

			libogc's callback spins on the mail with interrupts disabled, and Dolphin's DSP HLE sometimes
			raises the interrupt without one, freezing the console. Real hardware is unaffected.
		*/
		void GuardedDspCallback()
		{
			for (std::int32_t i = 0; i < 64; i++) {
				if (DSP_CheckMailFrom()) {
					libogcDspCallback();
					return;
				}
			}
		}
	}

	AsndAudioDevice::AsndAudioDevice()
		: _initialized(false)
	{
		LOGD("Initializing ASND audio device...");

		// Sets up the DSP mixer task and starts the audio DMA, including DSP_Init() and AUDIO_Init()
		ASND_Init();
		if (libogcDspCallback == nullptr) {
			libogcDspCallback = DSP_RegisterCallback(GuardedDspCallback);
		}
		// The mixer starts out paused
		ASND_Pause(0);
		_initialized = true;

		// Every voice is a source, they are all available from the start
		std::uint32_t sourceIds[MaxSources];
		for (std::int32_t i = 0; i < MaxSources; i++) {
			sourceIds[i] = std::uint32_t(i + 1);
		}
		SetSourcePool(arrayView(sourceIds, MaxSources));

		LOGI("--- ASND audio device info ---");
		LOGI("Output Frequency: {} Hz", OutputFrequency);
		LOGI("Voices: {}", MaxSources);
	}

	AsndAudioDevice::~AsndAudioDevice()
	{
		LOGD("Disposing ASND audio device...");

		// Shut down the decoding thread first, so it doesn't touch any readers afterwards
		ShutdownDecodeThread();

		if (_initialized) {
			for (std::int32_t i = 0; i < MaxSources; i++) {
				ASND_StopVoice(i);
			}
			ASND_End();
			_initialized = false;
		}

		for (Buffer& buffer : _buffers) {
			std::free(buffer.data);
			buffer.data = nullptr;
		}
	}

	bool AsndAudioDevice::IsValid() const
	{
		return _initialized;
	}

	const char* AsndAudioDevice::GetName() const
	{
		return "ASND";
	}

	void AsndAudioDevice::SetGain(float gain)
	{
		_gain = gain;

		// The DSP has no master volume, it is folded into the per-voice volume instead
		for (std::int32_t i = 0; i < MaxSources; i++) {
			ApplyVolume(i);
		}
	}

	void AsndAudioDevice::UpdateListener(const Vector3f& position, const Vector3f& velocity)
	{
		_listenerPos = position;

		for (std::int32_t i = 0; i < MaxSources; i++) {
			if (!_sources[i].relative) {
				ApplyVolume(i);
			}
		}
	}

	std::int32_t AsndAudioDevice::GetNativeFrequency()
	{
		// The DSP resamples for free, and decoding modules at 48 kHz would only cost more CPU
		return 32000;
	}

	std::uint32_t AsndAudioDevice::RegisterPlayer(IAudioPlayer* player)
	{
		const std::uint32_t sourceId = AudioDeviceBase::RegisterPlayer(player);

		// Start from a clean slate, so nothing (looping, streaming) is inherited from the previous owner
		const std::int32_t voice = GetVoiceIndex(sourceId);
		if (voice >= 0) {
			_sources[voice] = Source();
		}
		return sourceId;
	}

	void AsndAudioDevice::UpdatePlayers()
	{
		Pump();

		AudioDeviceBase::UpdatePlayers();
	}

	std::int32_t AsndAudioDevice::GetVoiceIndex(std::uint32_t sourceId)
	{
		if (sourceId == 0 || sourceId > std::uint32_t(MaxSources)) {
			return -1;
		}
		return std::int32_t(sourceId) - 1;
	}

	AsndAudioDevice::Buffer* AsndAudioDevice::FindBuffer(std::uint32_t bufferId)
	{
		if (bufferId == 0 || bufferId > _buffers.size()) {
			return nullptr;
		}
		Buffer& buffer = _buffers[bufferId - 1];
		return (buffer.used ? &buffer : nullptr);
	}

	std::uint32_t AsndAudioDevice::CreateBuffer(BufferUsage usage)
	{
		for (std::size_t i = 0; i < _buffers.size(); i++) {
			if (!_buffers[i].used) {
				_buffers[i].used = true;
				_buffers[i].size = 0;
				return std::uint32_t(i + 1);
			}
		}

		_buffers.emplace_back();
		_buffers.back().used = true;
		return std::uint32_t(_buffers.size());
	}

	void AsndAudioDevice::DeleteBuffer(std::uint32_t bufferId)
	{
		Buffer* buffer = FindBuffer(bufferId);
		if (buffer == nullptr) {
			return;
		}

		std::free(buffer->data);
		*buffer = Buffer();
	}

	bool AsndAudioDevice::UploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency)
	{
		Buffer* buffer = FindBuffer(bufferId);
		if (buffer == nullptr || size < 0) {
			return false;
		}

		// ASND flushes whole cache lines before DMA, so align and pad to a cache line
		const std::int32_t requiredCapacity = (size + BufferAlignment - 1) & ~(BufferAlignment - 1);
		if (requiredCapacity > buffer->capacity) {
			std::free(buffer->data);
			buffer->data = static_cast<std::uint8_t*>(::memalign(BufferAlignment, requiredCapacity));
			buffer->capacity = (buffer->data != nullptr ? requiredCapacity : 0);
			if (buffer->data == nullptr) {
				buffer->size = 0;
				LOGE("Cannot allocate {} bytes for an audio buffer", requiredCapacity);
				return false;
			}
		}

		if (size > 0 && data != nullptr) {
			std::memcpy(buffer->data, data, size);
		}
		if (requiredCapacity > size) {
			std::memset(buffer->data + size, 0, requiredCapacity - size);
		}

		buffer->size = size;
		buffer->frequency = frequency;
		buffer->voiceFormat = ToAsndVoiceFormat(format);
		return true;
	}

	void AsndAudioDevice::ComputeVolume(std::int32_t voice, std::int32_t& volumeLeft, std::int32_t& volumeRight)
	{
		const Source& source = _sources[voice];

		// A stereo buffer ignores its position, like in OpenAL
		const std::uint32_t bufferId = (source.streaming
			? (source.numQueued > 0 ? source.queuedBufferIds[0] : 0)
			: source.attachedBufferId);
		const Buffer* buffer = FindBuffer(bufferId);
		const bool isStereo = (buffer != nullptr && IsStereoFormat(buffer->voiceFormat));

		float left = _gain * source.gain;
		float right = left;

		if (!isStereo) {
			// Already in the listener's physical frame (see IAudioPlayer::GetAdjustedPosition)
			Vector3f relative = source.position;
			if (!source.relative) {
				relative -= Vector3f(_listenerPos.X * LengthToPhysical,
					_listenerPos.Y * -LengthToPhysical, _listenerPos.Z * -LengthToPhysical);
			}

			const float distance = relative.Length();

			// AL_LINEAR_DISTANCE_CLAMPED with rolloff 1, to match the OpenAL backend
			float attenuation = 1.0f;
			if (distance > ReferenceDistance) {
				const float clamped = (distance < MaxDistance ? distance : MaxDistance);
				attenuation = 1.0f - (clamped - ReferenceDistance) / (MaxDistance - ReferenceDistance);
			}

			// Constant-power panning, so a sound keeps its loudness as it crosses the centre
			float pan = 0.0f;
			if (distance > 0.0001f) {
				pan = relative.X / distance;
				pan = (pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan));
			}

			left *= attenuation * std::sqrt((1.0f - pan) * 0.5f);
			right *= attenuation * std::sqrt((1.0f + pan) * 0.5f);
		}

		const auto toVolume = [](float value) -> std::int32_t {
			const std::int32_t volume = std::int32_t(value * MaxVolume + 0.5f);
			return (volume < 0 ? 0 : (volume > MaxVolume ? MaxVolume : volume));
		};

		volumeLeft = toVolume(left);
		volumeRight = toVolume(right);
	}

	void AsndAudioDevice::ApplyVolume(std::int32_t voice)
	{
		if (!_sources[voice].started) {
			return;
		}

		std::int32_t volumeLeft, volumeRight;
		ComputeVolume(voice, volumeLeft, volumeRight);
		ASND_ChangeVolumeVoice(voice, volumeLeft, volumeRight);
	}

	void AsndAudioDevice::StartVoice(std::int32_t voice, const Buffer& buffer)
	{
		Source& source = _sources[voice];

		// ASND takes the playback rate in Hz, so the pitch scales the sample rate
		std::int32_t pitchHz = std::int32_t(buffer.frequency * source.pitch + 0.5f);
		if (pitchHz < MIN_PITCH) {
			pitchHz = MIN_PITCH;
		} else if (pitchHz > MAX_PITCH) {
			pitchHz = MAX_PITCH;
		}

		// The volume must be right on start, a later change would let the attack play at full volume
		source.started = true;
		std::int32_t volumeLeft, volumeRight;
		ComputeVolume(voice, volumeLeft, volumeRight);

		if (source.streaming) {
			ASND_SetVoice(voice, buffer.voiceFormat, pitchHz, 0, buffer.data, buffer.size,
				volumeLeft, volumeRight, StreamVoiceCallback);
		} else if (source.looping) {
			ASND_SetInfiniteVoice(voice, buffer.voiceFormat, pitchHz, 0, buffer.data, buffer.size,
				volumeLeft, volumeRight);
		} else {
			ASND_SetVoice(voice, buffer.voiceFormat, pitchHz, 0, buffer.data, buffer.size,
				volumeLeft, volumeRight, nullptr);
		}

		if (source.paused) {
			ASND_PauseVoice(voice, 1);
		}
	}

	void AsndAudioDevice::Pump()
	{
		for (std::int32_t voice = 0; voice < MaxSources; voice++) {
			Source& source = _sources[voice];
			if (!source.streaming || source.numQueued == 0) {
				continue;
			}

			// Count submitted buffers the DSP is done with as processed
			while (source.numProcessed < source.numSubmitted) {
				const Buffer* buffer = FindBuffer(source.queuedBufferIds[source.numProcessed]);
				if (buffer != nullptr && ASND_TestPointer(voice, buffer->data) == SND_BUSY) {
					break;
				}
				source.numProcessed++;
			}

			// Hand the voice as much of the rest of the queue as it can hold
			while (source.numSubmitted < source.numQueued &&
				   source.numSubmitted - source.numProcessed < MaxBuffersInFlight) {
				const Buffer* buffer = FindBuffer(source.queuedBufferIds[source.numSubmitted]);
				if (buffer == nullptr || buffer->size <= 0) {
					// Nothing playable, count it as done so the stream can reclaim it
					source.numSubmitted++;
					source.numProcessed = source.numSubmitted;
					continue;
				}

				if (!source.started || ASND_StatusVoice(voice) == SND_UNUSED) {
					StartVoice(voice, *buffer);
					source.numSubmitted++;
				} else if (ASND_AddVoice(voice, buffer->data, buffer->size) == SND_OK) {
					source.numSubmitted++;
				} else {
					// The second slot is still occupied, try again next frame
					break;
				}
			}
		}
	}

	void AsndAudioDevice::SetSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		const std::int32_t voice = GetVoiceIndex(sourceId);
		if (voice < 0) {
			return;
		}

		Source& source = _sources[voice];
		source.attachedBufferId = bufferId;

		if (bufferId == 0) {
			// Detaching releases the source, so reset everything
			if (source.started) {
				ASND_StopVoice(voice);
			}
			source.numQueued = 0;
			source.numSubmitted = 0;
			source.numProcessed = 0;
			source.streaming = false;
			source.started = false;
			source.paused = false;
		} else {
			source.streaming = false;
		}
	}

	void AsndAudioDevice::SetSourceGain(std::uint32_t sourceId, float gain)
	{
		const std::int32_t voice = GetVoiceIndex(sourceId);
		if (voice >= 0) {
			_sources[voice].gain = gain;
			ApplyVolume(voice);
		}
	}

	void AsndAudioDevice::SetSourcePitch(std::uint32_t sourceId, float pitch)
	{
		const std::int32_t voice = GetVoiceIndex(sourceId);
		if (voice < 0) {
			return;
		}

		Source& source = _sources[voice];
		source.pitch = pitch;

		if (source.started) {
			// The rate is relative to the sample rate of what is currently sounding
			const std::uint32_t bufferId = (source.streaming
				? (source.numQueued > 0 ? source.queuedBufferIds[0] : 0)
				: source.attachedBufferId);
			const Buffer* buffer = FindBuffer(bufferId);
			if (buffer != nullptr && buffer->frequency > 0) {
				std::int32_t pitchHz = std::int32_t(buffer->frequency * pitch + 0.5f);
				if (pitchHz < MIN_PITCH) {
					pitchHz = MIN_PITCH;
				} else if (pitchHz > MAX_PITCH) {
					pitchHz = MAX_PITCH;
				}
				ASND_ChangePitchVoice(voice, pitchHz);
			}
		}
	}

	void AsndAudioDevice::SetSourceLooping(std::uint32_t sourceId, bool looping)
	{
		const std::int32_t voice = GetVoiceIndex(sourceId);
		if (voice >= 0) {
			// Applied on the next start, ASND cannot switch a sounding voice
			_sources[voice].looping = looping;
		}
	}

	void AsndAudioDevice::SetSourceRelative(std::uint32_t sourceId, bool relative)
	{
		const std::int32_t voice = GetVoiceIndex(sourceId);
		if (voice >= 0) {
			_sources[voice].relative = relative;
			ApplyVolume(voice);
		}
	}

	void AsndAudioDevice::SetSourcePosition(std::uint32_t sourceId, const Vector3f& position)
	{
		const std::int32_t voice = GetVoiceIndex(sourceId);
		if (voice >= 0) {
			_sources[voice].position = position;
			ApplyVolume(voice);
		}
	}

	void AsndAudioDevice::SetSourceLowPass(std::uint32_t sourceId, float value)
	{
		// The DSP mixer has no filter stage, so sounds are not muffled underwater
	}

	std::int32_t AsndAudioDevice::GetSourceSampleOffset(std::uint32_t sourceId)
	{
		const std::int32_t voice = GetVoiceIndex(sourceId);
		if (voice < 0 || !_sources[voice].started) {
			return 0;
		}

		// The tick counter runs at the output rate, the caller wants samples of the source data
		const Source& source = _sources[voice];
		const Buffer* buffer = FindBuffer(source.streaming
			? (source.numQueued > 0 ? source.queuedBufferIds[0] : 0)
			: source.attachedBufferId);
		if (buffer == nullptr || buffer->frequency <= 0) {
			return 0;
		}
		return std::int32_t(std::uint64_t(ASND_GetTickCounterVoice(voice)) * buffer->frequency / OutputFrequency);
	}

	void AsndAudioDevice::SetSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset)
	{
		// ASND plays a buffer from its start, there is no way to seek within a voice
	}

	void AsndAudioDevice::PlaySource(std::uint32_t sourceId)
	{
		const std::int32_t voice = GetVoiceIndex(sourceId);
		if (voice < 0) {
			return;
		}

		Source& source = _sources[voice];

		if (source.paused) {
			source.paused = false;
			if (source.started) {
				ASND_PauseVoice(voice, 0);
				return;
			}
		}

		if (source.attachedBufferId != 0) {
			const Buffer* buffer = FindBuffer(source.attachedBufferId);
			if (buffer != nullptr && buffer->size > 0) {
				StartVoice(voice, *buffer);
			}
		} else {
			// Streaming, Pump() starts the voice once the first buffer is queued
			source.streaming = true;
		}
	}

	void AsndAudioDevice::PauseSource(std::uint32_t sourceId)
	{
		const std::int32_t voice = GetVoiceIndex(sourceId);
		if (voice < 0) {
			return;
		}

		_sources[voice].paused = true;
		if (_sources[voice].started) {
			ASND_PauseVoice(voice, 1);
		}
	}

	void AsndAudioDevice::StopSource(std::uint32_t sourceId)
	{
		const std::int32_t voice = GetVoiceIndex(sourceId);
		if (voice < 0) {
			return;
		}

		Source& source = _sources[voice];
		if (source.started) {
			ASND_StopVoice(voice);
			source.started = false;
		}
		source.paused = false;

		// Mark everything processed, so the owner can unqueue all buffers
		source.numSubmitted = 0;
		source.numProcessed = source.numQueued;
	}

	bool AsndAudioDevice::IsSourcePlaying(std::uint32_t sourceId)
	{
		const std::int32_t voice = GetVoiceIndex(sourceId);
		if (voice < 0) {
			return false;
		}

		const Source& source = _sources[voice];
		if (!source.started || source.paused) {
			return false;
		}
		return (ASND_StatusVoice(voice) == SND_WORKING);
	}

	void AsndAudioDevice::QueueBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		const std::int32_t voice = GetVoiceIndex(sourceId);
		if (voice < 0) {
			return;
		}

		Source& source = _sources[voice];
		if (source.numQueued >= MaxQueuedBuffers) {
			LOGW("Streaming queue of source {} is full, dropping a buffer", sourceId);
			return;
		}

		source.streaming = true;
		source.queuedBufferIds[source.numQueued] = bufferId;
		source.numQueued++;

		// Submit right away, so the stream starts this frame instead of the next
		Pump();
	}

	std::int32_t AsndAudioDevice::GetProcessedBufferCount(std::uint32_t sourceId)
	{
		const std::int32_t voice = GetVoiceIndex(sourceId);
		return (voice >= 0 ? _sources[voice].numProcessed : 0);
	}

	void AsndAudioDevice::UnqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds)
	{
		const std::int32_t voice = GetVoiceIndex(sourceId);
		if (voice < 0) {
			return;
		}

		Source& source = _sources[voice];
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
		source.numSubmitted -= count;
		if (source.numSubmitted < 0) {
			source.numSubmitted = 0;
		}
	}

	void AsndAudioDevice::SuspendDevice()
	{
		if (_initialized) {
			ASND_Pause(1);
		}
	}

	void AsndAudioDevice::ResumeDevice()
	{
		if (_initialized) {
			ASND_Pause(0);
		}
	}
}

#endif
