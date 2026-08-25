#if defined(WITH_AHIAUDIO)

#include "AmigaAudioDevice.h"
#include "../../AudioMixerCommon.h"
#include "../../IAudioPlayer.h"
#include "../../../Backends/Amiga/AmigaPlatform.h"
#include "../../../../Main.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

#include <exec/exec.h>
#include <exec/memory.h>
#include <devices/ahi.h>
#include <proto/exec.h>

namespace nCine
{
	namespace
	{
		std::int32_t GetFreeHeapBytes()
		{
			return std::int32_t(AvailMem(MEMF_ANY));
		}

		std::int32_t MixingRateForPreset()
		{
			// The mixer's cost is linear in this rate, so it is a preset knob like the resolution: the
			// fast tiers mix at CD-adjacent quality, the slow ones at the rate the original game shipped
			// most of its content at anyway. AHI resamples to whatever the user's mode runs at.
			using PerformanceClass = Backends::AmigaPlatform::PerformanceClass;
			switch (Backends::AmigaPlatform::GetPerformanceClass()) {
				case PerformanceClass::Ultra:
				case PerformanceClass::High:
					return 44100;
				default:
					return 22050;
			}
		}
	}

	AmigaAudioDevice::AmigaAudioDevice()
		: _valid(false), _suspended(false), _outputFrequency(22050), _replyPort(nullptr),
			_requests{}, _blocks{}, _inFlight{}, _lastQueued(nullptr), _deviceOpen(false), _linkSupported(false), _blockFrames(1024), _maxInFlight(4), _probeRequests{}, _lastRetireTicks(0),
			_buffers(nullptr), _bufferCount(0), _bufferCapacity(0), _mixBuffer(nullptr)
	{
		_outputFrequency = MixingRateForPreset();

		_replyPort = CreateMsgPort();
		if (_replyPort == nullptr) {
			LOGE("Cannot create the AHI reply port, sound will be disabled");
			return;
		}

		// The device is opened on the first request; the others are byte copies of it, which is the
		// documented ahi.device double-buffering arrangement (they share the opened unit)
		_requests[0] = reinterpret_cast<AHIRequest*>(CreateIORequest(_replyPort, sizeof(AHIRequest)));
		if (_requests[0] == nullptr) {
			LOGE("Cannot create the AHI request, sound will be disabled");
			return;
		}
		_requests[0]->ahir_Version = 4;
		if (OpenDevice(reinterpret_cast<CONST_STRPTR>(AHINAME), AHI_DEFAULT_UNIT, reinterpret_cast<struct IORequest*>(_requests[0]), 0) != 0) {
			LOGE("Cannot open ahi.device unit 0 (is AHI installed and configured?), sound will be disabled");
			DeleteIORequest(reinterpret_cast<struct IORequest*>(_requests[0]));
			_requests[0] = nullptr;
			return;
		}
		_deviceOpen = true;

		for (std::int32_t i = 1; i < BlockCount; i++) {
			_requests[i] = reinterpret_cast<AHIRequest*>(CreateIORequest(_replyPort, sizeof(AHIRequest)));
			if (_requests[i] == nullptr) {
				LOGE("Cannot create the AHI requests ({} bytes of memory free), sound will be disabled",
					GetFreeHeapBytes());
				return;
			}
			// Clone the opened request: same device, same unit, own message
			struct Message preserved = _requests[i]->ahir_Std.io_Message;
			std::memcpy(_requests[i], _requests[0], sizeof(AHIRequest));
			_requests[i]->ahir_Std.io_Message = preserved;
		}

		bool outOfMemory = false;
		const std::int32_t blockBytes = MaxBlockFrames * ChannelCount * std::int32_t(sizeof(std::int16_t));
		for (std::int32_t i = 0; i < BlockCount && !outOfMemory; i++) {
			_blocks[i] = static_cast<std::int16_t*>(std::malloc(std::size_t(blockBytes)));
			outOfMemory = (_blocks[i] == nullptr);
		}
		_mixBuffer = static_cast<std::int32_t*>(std::malloc(std::size_t(MaxBlockFrames) * ChannelCount * sizeof(std::int32_t)));
		_buffers = static_cast<Buffer*>(std::malloc(std::size_t(InitialBufferCapacity) * sizeof(Buffer)));
		if (outOfMemory || _mixBuffer == nullptr || _buffers == nullptr) {
			LOGE("Cannot allocate the AHI mixing buffers ({} bytes of memory free), sound will be disabled",
				GetFreeHeapBytes());
			return;
		}
		_bufferCapacity = InitialBufferCapacity;

		using PerformanceClass = Backends::AmigaPlatform::PerformanceClass;
		const PerformanceClass performanceClass = Backends::AmigaPlatform::GetPerformanceClass();
		const bool slowTier = (performanceClass != PerformanceClass::Ultra && performanceClass != PerformanceClass::High);
		if (slowTier) {
			// A long frame is the norm here, so the queue is sized to cover one - and links are given up,
			// because only an unlinked queue can be this deep. The probe below is skipped along with them:
			// it plays two silent blocks out and waits for the reply, which is startup time this tier can
			// least afford spending on an answer that is not used
			_linkSupported = false;
			_blockFrames = MaxBlockFrames;
			_maxInFlight = BlockCount;
		} else {
			_linkSupported = ProbeLinkSupport(blockBytes);
			_blockFrames = (_linkSupported ? 2048 : 1024);
			_maxInFlight = (_linkSupported ? 2 : 4);
		}

		// Buffer id 0 is reserved as "no buffer"
		::new (&_buffers[0]) Buffer{};
		_bufferCount = 1;

		std::uint32_t sourceIds[MaxSources];
		for (std::int32_t i = 0; i < MaxSources; i++) {
			sourceIds[i] = std::uint32_t(i + 1);
		}
		setSourcePool(arrayView(sourceIds, MaxSources));

		_valid = true;
		LOGI("Audio device initialized: ahi.device, mixing {} Hz stereo into {} frames per block, {} in flight, {}",
			_outputFrequency, _blockFrames, _maxInFlight, (_linkSupported ? "linked (gapless)" : "unlinked"));
	}

	AmigaAudioDevice::~AmigaAudioDevice()
	{
		shutdownDecodeThread();

		for (std::int32_t i = 0; i < 2; i++) {
			if (_probeRequests[i] != nullptr) {
				DeleteIORequest(reinterpret_cast<struct IORequest*>(_probeRequests[i]));
				_probeRequests[i] = nullptr;
			}
		}

		if (_deviceOpen) {
			for (std::int32_t i = 0; i < BlockCount; i++) {
				if (_inFlight[i]) {
					AbortIO(reinterpret_cast<struct IORequest*>(_requests[i]));
					WaitIO(reinterpret_cast<struct IORequest*>(_requests[i]));
					_inFlight[i] = false;
				}
			}
			CloseDevice(reinterpret_cast<struct IORequest*>(_requests[0]));
			_deviceOpen = false;
		}
		for (std::int32_t i = 0; i < BlockCount; i++) {
			if (_requests[i] != nullptr) {
				DeleteIORequest(reinterpret_cast<struct IORequest*>(_requests[i]));
				_requests[i] = nullptr;
			}
			std::free(_blocks[i]);
			_blocks[i] = nullptr;
		}
		if (_replyPort != nullptr) {
			DeleteMsgPort(_replyPort);
			_replyPort = nullptr;
		}

		for (std::int32_t i = 0; i < _bufferCount; i++) {
			ReleaseBuffer(_buffers[i]);
		}
		std::free(_buffers);
		_buffers = nullptr;
		_bufferCount = 0;
		_bufferCapacity = 0;

		std::free(_mixBuffer);
		_mixBuffer = nullptr;
	}

	bool AmigaAudioDevice::isValid() const
	{
		return _valid;
	}

	const char* AmigaAudioDevice::name() const
	{
		return "ahi.device";
	}

	void AmigaAudioDevice::setGain(float gain)
	{
		// Applied while mixing; ahir_Volume stays at unity
		_gain = gain;
	}

	void AmigaAudioDevice::updateListener(const Vector3f& position, const Vector3f& velocity)
	{
		static_cast<void>(velocity);
		_listenerPos = position;
	}

	std::int32_t AmigaAudioDevice::nativeFrequency()
	{
		return _outputFrequency;
	}

	std::uint32_t AmigaAudioDevice::registerPlayer(IAudioPlayer* player)
	{
		const std::uint32_t sourceId = AudioDeviceBase::registerPlayer(player);
		if (sourceId != UnavailableSource) {
			if (Source* source = GetSource(sourceId)) {
				*source = Source{};
			}
		}
		return sourceId;
	}

	AmigaAudioDevice::Source* AmigaAudioDevice::GetSource(std::uint32_t sourceId)
	{
		if (sourceId == 0 || sourceId > std::uint32_t(MaxSources)) {
			return nullptr;
		}
		return &_sources[sourceId - 1];
	}

	std::uint32_t AmigaAudioDevice::createBuffer(BufferUsage usage)
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
				LOGE("Cannot grow the audio buffer table to {} entries ({} bytes of memory free)",
					newCapacity, GetFreeHeapBytes());
				return 0;
			}
			_buffers = grown;
			_bufferCapacity = newCapacity;
		}

		::new (&_buffers[_bufferCount]) Buffer{};
		_buffers[_bufferCount].Used = true;
		return std::uint32_t(_bufferCount++);
	}

	void AmigaAudioDevice::ReleaseBuffer(Buffer& buffer)
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

	void AmigaAudioDevice::deleteBuffer(std::uint32_t bufferId)
	{
		if (bufferId == 0 || bufferId >= std::uint32_t(_bufferCount)) {
			return;
		}
		ReleaseBuffer(_buffers[bufferId]);
		_buffers[bufferId].Used = false;
	}

	bool AmigaAudioDevice::uploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency)
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
				LOGE("Cannot allocate {} bytes for audio buffer {} ({} bytes of memory free), the sound will be silent",
					byteCount, bufferId, GetFreeHeapBytes());
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

	void AmigaAudioDevice::setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->BufferId = bufferId;
			source->Cursor = 0;
		}
	}

	void AmigaAudioDevice::setSourceGain(std::uint32_t sourceId, float gain)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Gain = gain;
		}
	}

	void AmigaAudioDevice::setSourcePitch(std::uint32_t sourceId, float pitch)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Pitch = (pitch > 0.0f ? pitch : 1.0f);
		}
	}

	void AmigaAudioDevice::setSourceLooping(std::uint32_t sourceId, bool looping)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Looping = looping;
		}
	}

	void AmigaAudioDevice::setSourceRelative(std::uint32_t sourceId, bool relative)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Relative = relative;
		}
	}

	void AmigaAudioDevice::setSourcePosition(std::uint32_t sourceId, const Vector3f& position)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Position = position;
		}
	}

	void AmigaAudioDevice::setSourceLowPass(std::uint32_t sourceId, float value)
	{
		// No per-voice DSP in this mixer (see the N64 backend for why the branch is not worth it)
		static_cast<void>(sourceId);
		static_cast<void>(value);
	}

	std::int32_t AmigaAudioDevice::sourceSampleOffset(std::uint32_t sourceId)
	{
		Source* source = GetSource(sourceId);
		return (source != nullptr ? std::int32_t(source->Cursor >> 32) : 0);
	}

	void AmigaAudioDevice::setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Cursor = std::int64_t(offset > 0 ? offset : 0) << 32;
		}
	}

	void AmigaAudioDevice::playSource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Playing = true;
			source->Paused = false;
		}
	}

	void AmigaAudioDevice::pauseSource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Paused = true;
		}
	}

	void AmigaAudioDevice::stopSource(std::uint32_t sourceId)
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

	bool AmigaAudioDevice::isSourcePlaying(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr && source->Playing && !source->Paused);
	}

	void AmigaAudioDevice::queueBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
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

	std::int32_t AmigaAudioDevice::numProcessedBuffers(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr ? source->ProcessedCount : 0);
	}

	void AmigaAudioDevice::unqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds)
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

	void AmigaAudioDevice::suspendDevice()
	{
		// Stop feeding and cut what is queued: unlike a hardware ring there is up to ~90 ms in flight,
		// and a suspend should silence promptly
		for (std::int32_t i = 0; i < BlockCount; i++) {
			if (_inFlight[i]) {
				AbortIO(reinterpret_cast<struct IORequest*>(_requests[i]));
				WaitIO(reinterpret_cast<struct IORequest*>(_requests[i]));
				_inFlight[i] = false;
			}
		}
		_lastQueued = nullptr;
		_suspended = true;
	}

	void AmigaAudioDevice::resumeDevice()
	{
		_suspended = false;
	}

	void AmigaAudioDevice::updatePlayers()
	{
		AudioDeviceBase::updatePlayers();

		if (_valid && !_suspended) {
			FillQueue();
		}
	}

	bool AmigaAudioDevice::ProbeLinkSupport(std::int32_t blockBytes)
	{
		for (std::int32_t i = 0; i < 2; i++) {
			_probeRequests[i] = reinterpret_cast<AHIRequest*>(CreateIORequest(_replyPort, sizeof(AHIRequest)));
			if (_probeRequests[i] == nullptr) {
				// Without both there is nothing to measure, and unlinked playback is the safe answer
				return false;
			}
			struct Message preserved = _probeRequests[i]->ahir_Std.io_Message;
			std::memcpy(_probeRequests[i], _requests[0], sizeof(AHIRequest));
			_probeRequests[i]->ahir_Std.io_Message = preserved;
		}

		for (std::int32_t i = 0; i < 2; i++) {
			std::memset(_blocks[i], 0, std::size_t(blockBytes));

			AHIRequest* request = _probeRequests[i];
			request->ahir_Std.io_Message.mn_Node.ln_Pri = 0;
			request->ahir_Std.io_Command = CMD_WRITE;
			request->ahir_Std.io_Data = _blocks[i];
			request->ahir_Std.io_Length = ULONG(blockBytes);
			request->ahir_Std.io_Offset = 0;
			request->ahir_Type = AHIST_S16S;
			request->ahir_Frequency = ULONG(_outputFrequency);
			request->ahir_Volume = 0x10000;
			request->ahir_Position = 0x8000;
			request->ahir_Link = (i == 0 ? nullptr : _probeRequests[0]);
			SendIO(reinterpret_cast<struct IORequest*>(request));
		}

		// The first block is replied once the second one takes over from it, which cannot happen before
		// the first has played out; a second of grace is many times that and still imperceptible here,
		// because what is playing is silence and the game is still loading
		bool linkSupported = false;
		const std::uint64_t deadline = Backends::AmigaPlatform::TimerTicks() +
			std::uint64_t(Backends::AmigaPlatform::TimerFrequency());
		while (Backends::AmigaPlatform::TimerTicks() < deadline) {
			if (CheckIO(reinterpret_cast<struct IORequest*>(_probeRequests[0])) != nullptr) {
				linkSupported = true;
				break;
			}
			// Polled rather than slept through dos.library: `proto/dos.h` cannot be included here (its
			// DateTime clashes with Death::Containers::DateTime), and AHI mixes at a higher priority
			// than this task, so spinning for the moment it takes cannot starve the playback itself
		}

		for (std::int32_t i = 1; i >= 0; i--) {
			AbortIO(reinterpret_cast<struct IORequest*>(_probeRequests[i]));
			WaitIO(reinterpret_cast<struct IORequest*>(_probeRequests[i]));
		}
		while (GetMsg(_replyPort) != nullptr) {
			// Anything still queued on the port belongs to the two probe requests
		}

		if (!linkSupported) {
			LOGW("This ahi.device does not reply linked requests, playing blocks unlinked instead");
		}
		return linkSupported;
	}

	void AmigaAudioDevice::RecoverStalledQueue()
	{
		static std::int32_t recoveries = 0;
		if (recoveries == 0) {
			LOGW("Audio queue stalled, restarting it (this ahi.device stops servicing a queue that ran dry)");
		}
		recoveries++;

		for (std::int32_t i = 0; i < BlockCount; i++) {
			if (_inFlight[i]) {
				AbortIO(reinterpret_cast<struct IORequest*>(_requests[i]));
				WaitIO(reinterpret_cast<struct IORequest*>(_requests[i]));
				_inFlight[i] = false;
			}
		}
		while (GetMsg(_replyPort) != nullptr) {
			// Replies that arrived while the requests were being aborted
		}
		_lastQueued = nullptr;
	}

	void AmigaAudioDevice::FillQueue()
	{
		// Retire finished requests first (their messages sit on the reply port), then top the queue up,
		// bounded so a late frame cannot spend itself catching up - the same discipline as the N64/PS3
		// backends. GetMsg never blocks, and neither does SendIO.
		struct Message* message;
		bool anyRetired = false;
		while ((message = GetMsg(_replyPort)) != nullptr) {
			for (std::int32_t i = 0; i < BlockCount; i++) {
				if (reinterpret_cast<struct Message*>(_requests[i]) == message) {
					_inFlight[i] = false;
					anyRetired = true;
					if (_lastQueued == _requests[i]) {
						_lastQueued = nullptr;
					}
					break;
				}
			}
		}

		const std::uint64_t now = Backends::AmigaPlatform::TimerTicks();
		if (anyRetired || _lastRetireTicks == 0) {
			_lastRetireTicks = now;
		}

		std::int32_t inFlightCount = 0;
		for (std::int32_t i = 0; i < BlockCount; i++) {
			if (_inFlight[i]) {
				inFlightCount++;
			}
		}


		// A full queue that has not moved for a second is not slow, it is stuck (see RecoverStalledQueue)
		if (inFlightCount >= _maxInFlight &&
			now - _lastRetireTicks > std::uint64_t(Backends::AmigaPlatform::TimerFrequency())) {
			RecoverStalledQueue();
			_lastRetireTicks = Backends::AmigaPlatform::TimerTicks();
			inFlightCount = 0;
		}


		for (std::int32_t i = 0; i < BlockCount && inFlightCount < _maxInFlight; i++) {
			if (_inFlight[i]) {
				continue;
			}
			MixInto(_blocks[i], _blockFrames);

			AHIRequest* request = _requests[i];
			request->ahir_Std.io_Message.mn_Node.ln_Pri = 0;
			request->ahir_Std.io_Command = CMD_WRITE;
			request->ahir_Std.io_Data = _blocks[i];
			request->ahir_Std.io_Length = ULONG(_blockFrames) * ChannelCount * sizeof(std::int16_t);
			request->ahir_Std.io_Offset = 0;
			request->ahir_Type = AHIST_S16S;
			request->ahir_Frequency = ULONG(_outputFrequency);
			request->ahir_Volume = 0x10000;   // unity - the mixer applied the gains already
			request->ahir_Position = 0x8000;  // center
			// Linking to the one still-playing request is what makes consecutive blocks gapless. The
			// link target must not have finished already - a request linked to a replied one never
			// plays - and it can finish between the drain above and here, so its completion is
			// re-tested rather than taken from _inFlight
			AHIRequest* link = (_linkSupported ? _lastQueued : nullptr);
			if (link != nullptr && CheckIO(reinterpret_cast<struct IORequest*>(link)) != nullptr) {
				link = nullptr;
			}
			request->ahir_Link = link;
			SendIO(reinterpret_cast<struct IORequest*>(request));
			_inFlight[i] = true;
			_lastQueued = request;
			inFlightCount++;
		}
	}

	AmigaAudioDevice::Buffer* AmigaAudioDevice::GetActiveBuffer(Source& source)
	{
		const std::uint32_t bufferId = (source.QueueCount > 0 ? source.Queue[0] : source.BufferId);
		if (bufferId == 0 || bufferId >= std::uint32_t(_bufferCount)) {
			return nullptr;
		}
		Buffer& buffer = _buffers[bufferId];
		return (buffer.Used && buffer.Samples != nullptr && buffer.FrameCount > 0 ? &buffer : nullptr);
	}

	DEATH_ALWAYS_INLINE void AmigaAudioDevice::ReadFrame(const Buffer& buffer, std::int32_t frame, std::int32_t& left, std::int32_t& right)
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

	void AmigaAudioDevice::ComputePanning(const Source& source, float& leftGain, float& rightGain) const
	{
		AudioMixer::ComputeStereoGains(source.Relative, source.Position, _listenerPos, source.Gain, _gain, leftGain, rightGain);
	}

	bool AmigaAudioDevice::MixSource(Source& source, std::int32_t* output, std::int32_t frames)
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

	void AmigaAudioDevice::MixInto(std::int16_t* output, std::int32_t frames)
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
