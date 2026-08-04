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

		std::int32_t asndVoiceFormat(IAudioDevice::BufferFormat format)
		{
			// The samples are native-endian by the time they reach a buffer (the WAV and Vorbis readers
			// swap them on big-endian targets and the module decoder produces them that way), so the
			// big-endian formats are the ones to use here. 8-bit PCM is unsigned, like AL_FORMAT_MONO8.
			switch (format) {
				case IAudioDevice::BufferFormat::Stereo8: return VOICE_STEREO_8BIT_U;
				case IAudioDevice::BufferFormat::Mono16: return VOICE_MONO_16BIT_BE;
				case IAudioDevice::BufferFormat::Stereo16: return VOICE_STEREO_16BIT_BE;
				default: return VOICE_MONO_8BIT_U;
			}
		}

		bool isStereoFormat(std::int32_t voiceFormat)
		{
			return (voiceFormat == VOICE_STEREO_8BIT_U || voiceFormat == VOICE_STEREO_16BIT_BE);
		}

		// Keeps a streaming voice alive while its queue is momentarily empty. ASND frees a voice as soon
		// as it runs out of data unless it has a callback, which would end the music on any refill that
		// arrives a frame late. Nothing is done here - this runs in the DSP interrupt handler, and the
		// queue is refilled from pump() on the main thread instead.
		void streamVoiceCallback(s32 voice)
		{
		}
	}

	AsndAudioDevice::AsndAudioDevice()
		: _initialized(false)
	{
		LOGD("Initializing ASND audio device...");

		// Sets up the DSP mixer task and starts the audio DMA, including DSP_Init() and AUDIO_Init()
		ASND_Init();
		// The mixer starts out paused
		ASND_Pause(0);
		_initialized = true;

		// Every voice is a source, they are all available from the start
		std::uint32_t sourceIds[MaxSources];
		for (std::int32_t i = 0; i < MaxSources; i++) {
			sourceIds[i] = std::uint32_t(i + 1);
		}
		setSourcePool(arrayView(sourceIds, MaxSources));

		LOGI("--- ASND audio device info ---");
		LOGI("Output Frequency: {} Hz", OutputFrequency);
		LOGI("Voices: {}", MaxSources);
	}

	AsndAudioDevice::~AsndAudioDevice()
	{
		LOGD("Disposing ASND audio device...");

		// Shut down the decoding thread first, so it doesn't touch any readers afterwards
		shutdownDecodeThread();

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

	bool AsndAudioDevice::isValid() const
	{
		return _initialized;
	}

	const char* AsndAudioDevice::name() const
	{
		return "ASND";
	}

	void AsndAudioDevice::setGain(float gain)
	{
		_gain = gain;

		// The DSP has no master volume, it is folded into the per-voice volume instead
		for (std::int32_t i = 0; i < MaxSources; i++) {
			applyVolume(i);
		}
	}

	void AsndAudioDevice::updateListener(const Vector3f& position, const Vector3f& velocity)
	{
		_listenerPos = position;

		// Moving the listener changes the panning and attenuation of every source that is not relative
		// to it, which the mix here has to be told about explicitly
		for (std::int32_t i = 0; i < MaxSources; i++) {
			if (!_sources[i].relative) {
				applyVolume(i);
			}
		}
	}

	std::int32_t AsndAudioDevice::nativeFrequency()
	{
		// The DSP resamples every voice to its own output rate for free, so a stream does not have to be
		// decoded at 48 kHz. Module music is by far the most expensive thing decoded here and the samples
		// inside the original modules were recorded at around this rate to begin with.
		return 32000;
	}

	std::uint32_t AsndAudioDevice::registerPlayer(IAudioPlayer* player)
	{
		const std::uint32_t sourceId = AudioDeviceBase::registerPlayer(player);

		// A voice handed to a new player starts from a clean slate. Every player does release its
		// source properly, but the state here is what decides whether a voice loops or streams, and
		// inheriting any of it from the previous owner would be a silent and confusing bug.
		const std::int32_t voice = voiceForId(sourceId);
		if (voice >= 0) {
			_sources[voice] = Source();
		}
		return sourceId;
	}

	void AsndAudioDevice::updatePlayers()
	{
		pump();

		AudioDeviceBase::updatePlayers();
	}

	std::int32_t AsndAudioDevice::voiceForId(std::uint32_t sourceId)
	{
		if (sourceId == 0 || sourceId > std::uint32_t(MaxSources)) {
			return -1;
		}
		return std::int32_t(sourceId) - 1;
	}

	AsndAudioDevice::Buffer* AsndAudioDevice::bufferForId(std::uint32_t bufferId)
	{
		if (bufferId == 0 || bufferId > _buffers.size()) {
			return nullptr;
		}
		Buffer& buffer = _buffers[bufferId - 1];
		return (buffer.used ? &buffer : nullptr);
	}

	std::uint32_t AsndAudioDevice::createBuffer(BufferUsage usage)
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

	void AsndAudioDevice::deleteBuffer(std::uint32_t bufferId)
	{
		Buffer* buffer = bufferForId(bufferId);
		if (buffer == nullptr) {
			return;
		}

		std::free(buffer->data);
		*buffer = Buffer();
	}

	bool AsndAudioDevice::uploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency)
	{
		Buffer* buffer = bufferForId(bufferId);
		if (buffer == nullptr || size < 0) {
			return false;
		}

		// ASND reads the samples by DMA and flushes them out of the data cache by whole lines, so the
		// block has to be aligned and padded to a cache line - otherwise the flush would write back
		// whatever shares the first and last line with it
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
		buffer->voiceFormat = asndVoiceFormat(format);
		return true;
	}

	void AsndAudioDevice::computeVolume(std::int32_t voice, std::int32_t& volumeLeft, std::int32_t& volumeRight)
	{
		const Source& source = _sources[voice];

		// Which buffer is sounding decides whether the position is used at all: OpenAL plays a stereo
		// buffer straight to the output and ignores its position, and that is matched here
		const std::uint32_t bufferId = (source.streaming
			? (source.numQueued > 0 ? source.queuedBufferIds[0] : 0)
			: source.attachedBufferId);
		const Buffer* buffer = bufferForId(bufferId);
		const bool isStereo = (buffer != nullptr && isStereoFormat(buffer->voiceFormat));

		float left = _gain * source.gain;
		float right = left;

		if (!isStereo) {
			// The player hands over a position in the same physical frame the listener lives in (see
			// IAudioPlayer::getAdjustedPosition), so it only has to be made relative to the listener
			Vector3f relative = source.position;
			if (!source.relative) {
				relative -= Vector3f(_listenerPos.X * LengthToPhysical,
					_listenerPos.Y * -LengthToPhysical, _listenerPos.Z * -LengthToPhysical);
			}

			const float distance = relative.Length();

			// AL_LINEAR_DISTANCE_CLAMPED with a rolloff factor of 1, which is what the OpenAL backend
			// asks for, so a sound is equally loud on both backends
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

	void AsndAudioDevice::applyVolume(std::int32_t voice)
	{
		if (!_sources[voice].started) {
			return;
		}

		std::int32_t volumeLeft, volumeRight;
		computeVolume(voice, volumeLeft, volumeRight);
		ASND_ChangeVolumeVoice(voice, volumeLeft, volumeRight);
	}

	void AsndAudioDevice::startVoice(std::int32_t voice, const Buffer& buffer)
	{
		Source& source = _sources[voice];

		// ASND expresses the playback rate as the sample rate of the source data, so the pitch
		// multiplier scales it. The DSP resamples anything up to 144 kHz, but past its own output
		// rate it costs noticeably more, so a pitch that high is not worth honouring.
		std::int32_t pitchHz = std::int32_t(buffer.frequency * source.pitch + 0.5f);
		if (pitchHz < MIN_PITCH) {
			pitchHz = MIN_PITCH;
		} else if (pitchHz > MAX_PITCH) {
			pitchHz = MAX_PITCH;
		}

		// The volume has to be right in the very first call: ASND_ChangeVolumeVoice() only takes effect
		// when the DSP next picks the voice up, so starting at full volume and correcting afterwards
		// would play the attack of every sound at full loudness regardless of its distance
		source.started = true;
		std::int32_t volumeLeft, volumeRight;
		computeVolume(voice, volumeLeft, volumeRight);

		if (source.streaming) {
			ASND_SetVoice(voice, buffer.voiceFormat, pitchHz, 0, buffer.data, buffer.size,
				volumeLeft, volumeRight, streamVoiceCallback);
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

	void AsndAudioDevice::pump()
	{
		for (std::int32_t voice = 0; voice < MaxSources; voice++) {
			Source& source = _sources[voice];
			if (!source.streaming || source.numQueued == 0) {
				continue;
			}

			// Move every buffer the DSP is done with to the processed count. Only submitted buffers are
			// tested, and a submitted one is either sounding, waiting in the voice's second slot, or
			// finished - which is exactly what ASND_TestPointer() distinguishes.
			while (source.numProcessed < source.numSubmitted) {
				const Buffer* buffer = bufferForId(source.queuedBufferIds[source.numProcessed]);
				if (buffer != nullptr && ASND_TestPointer(voice, buffer->data) == SND_BUSY) {
					break;
				}
				source.numProcessed++;
			}

			// Hand the voice as much of the rest of the queue as it can hold
			while (source.numSubmitted < source.numQueued &&
				   source.numSubmitted - source.numProcessed < MaxBuffersInFlight) {
				const Buffer* buffer = bufferForId(source.queuedBufferIds[source.numSubmitted]);
				if (buffer == nullptr || buffer->size <= 0) {
					// Nothing playable, count it as done so the stream can reclaim it
					source.numSubmitted++;
					source.numProcessed = source.numSubmitted;
					continue;
				}

				if (!source.started || ASND_StatusVoice(voice) == SND_UNUSED) {
					startVoice(voice, *buffer);
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

	void AsndAudioDevice::setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		const std::int32_t voice = voiceForId(sourceId);
		if (voice < 0) {
			return;
		}

		Source& source = _sources[voice];
		source.attachedBufferId = bufferId;

		if (bufferId == 0) {
			// Detaching is how a player releases a source, so everything about the voice is reset here
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

	void AsndAudioDevice::setSourceGain(std::uint32_t sourceId, float gain)
	{
		const std::int32_t voice = voiceForId(sourceId);
		if (voice >= 0) {
			_sources[voice].gain = gain;
			applyVolume(voice);
		}
	}

	void AsndAudioDevice::setSourcePitch(std::uint32_t sourceId, float pitch)
	{
		const std::int32_t voice = voiceForId(sourceId);
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
			const Buffer* buffer = bufferForId(bufferId);
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

	void AsndAudioDevice::setSourceLooping(std::uint32_t sourceId, bool looping)
	{
		const std::int32_t voice = voiceForId(sourceId);
		if (voice >= 0) {
			// Taken into account when the voice is started, ASND decides between a one-shot and a
			// looping voice at that point and cannot be told to switch afterwards
			_sources[voice].looping = looping;
		}
	}

	void AsndAudioDevice::setSourceRelative(std::uint32_t sourceId, bool relative)
	{
		const std::int32_t voice = voiceForId(sourceId);
		if (voice >= 0) {
			_sources[voice].relative = relative;
			applyVolume(voice);
		}
	}

	void AsndAudioDevice::setSourcePosition(std::uint32_t sourceId, const Vector3f& position)
	{
		const std::int32_t voice = voiceForId(sourceId);
		if (voice >= 0) {
			_sources[voice].position = position;
			applyVolume(voice);
		}
	}

	void AsndAudioDevice::setSourceLowPass(std::uint32_t sourceId, float value)
	{
		// The DSP mixer has no filter stage, so sounds are not muffled underwater
	}

	std::int32_t AsndAudioDevice::sourceSampleOffset(std::uint32_t sourceId)
	{
		const std::int32_t voice = voiceForId(sourceId);
		if (voice < 0 || !_sources[voice].started) {
			return 0;
		}

		// The tick counter runs at the output rate, the caller wants samples of the source data
		const Source& source = _sources[voice];
		const Buffer* buffer = bufferForId(source.streaming
			? (source.numQueued > 0 ? source.queuedBufferIds[0] : 0)
			: source.attachedBufferId);
		if (buffer == nullptr || buffer->frequency <= 0) {
			return 0;
		}
		return std::int32_t(std::uint64_t(ASND_GetTickCounterVoice(voice)) * buffer->frequency / OutputFrequency);
	}

	void AsndAudioDevice::setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset)
	{
		// ASND plays a buffer from its start, there is no way to seek within a voice
	}

	void AsndAudioDevice::playSource(std::uint32_t sourceId)
	{
		const std::int32_t voice = voiceForId(sourceId);
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
			const Buffer* buffer = bufferForId(source.attachedBufferId);
			if (buffer != nullptr && buffer->size > 0) {
				startVoice(voice, *buffer);
			}
		} else {
			// A streaming player starts before it has queued anything, pump() picks the voice up as
			// soon as the first buffer arrives
			source.streaming = true;
		}
	}

	void AsndAudioDevice::pauseSource(std::uint32_t sourceId)
	{
		const std::int32_t voice = voiceForId(sourceId);
		if (voice < 0) {
			return;
		}

		_sources[voice].paused = true;
		if (_sources[voice].started) {
			ASND_PauseVoice(voice, 1);
		}
	}

	void AsndAudioDevice::stopSource(std::uint32_t sourceId)
	{
		const std::int32_t voice = voiceForId(sourceId);
		if (voice < 0) {
			return;
		}

		Source& source = _sources[voice];
		if (source.started) {
			ASND_StopVoice(voice);
			source.started = false;
		}
		source.paused = false;

		// A stopped source hands every buffer it still holds back to its owner, which reclaims them
		// through numProcessedBuffers() and unqueueBuffers() right after this
		source.numSubmitted = 0;
		source.numProcessed = source.numQueued;
	}

	bool AsndAudioDevice::isSourcePlaying(std::uint32_t sourceId)
	{
		const std::int32_t voice = voiceForId(sourceId);
		if (voice < 0) {
			return false;
		}

		const Source& source = _sources[voice];
		if (!source.started || source.paused) {
			return false;
		}
		return (ASND_StatusVoice(voice) == SND_WORKING);
	}

	void AsndAudioDevice::queueBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		const std::int32_t voice = voiceForId(sourceId);
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

		// Submit right away when the voice has room, so the first buffer of a stream starts sounding
		// in the same frame it was decoded in instead of one later
		pump();
	}

	std::int32_t AsndAudioDevice::numProcessedBuffers(std::uint32_t sourceId)
	{
		const std::int32_t voice = voiceForId(sourceId);
		return (voice >= 0 ? _sources[voice].numProcessed : 0);
	}

	void AsndAudioDevice::unqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds)
	{
		const std::int32_t voice = voiceForId(sourceId);
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

	void AsndAudioDevice::suspendDevice()
	{
		if (_initialized) {
			ASND_Pause(1);
		}
	}

	void AsndAudioDevice::resumeDevice()
	{
		if (_initialized) {
			ASND_Pause(0);
		}
	}
}

#endif
