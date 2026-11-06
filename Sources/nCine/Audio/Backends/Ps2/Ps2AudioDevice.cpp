#if defined(WITH_PS2AUDIO)

#include "Ps2AudioDevice.h"
#include "../../AudioMixerCommon.h"
#include "../../IAudioPlayer.h"
#include "../../../Backends/Ps2/Ps2Modules.h"
#include "../../../../Main.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <new>
#include <type_traits>

#include <Containers/StringView.h>

extern "C" {
#include <kernel.h>
#include <delaythread.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <audsrv.h>
}

using namespace Death::Containers::Literals;

namespace nCine
{
	namespace
	{
		/** @brief Bytes one frame of the stream occupies - 16-bit stereo is the only format this programs */
		constexpr std::int32_t FrameBytes = 2 * std::int32_t(sizeof(std::int16_t));
	}

	bool Ps2AudioDevice::InitializeModules()
	{
		// Runs at most once per process, an IRX cannot be loaded twice
		static bool attempted = false;
		static bool succeeded = false;
		if (attempted) {
			return succeeded;
		}
		attempted = true;

		// audsrv drives the SPU2 through `libsd`, so that goes first
		const int libsdId = SifLoadModule("rom0:LIBSD", 0, nullptr);
		if (libsdId < 0) {
			LOGE("SifLoadModule(\"rom0:LIBSD\") failed with error {}, sound will be disabled", libsdId);
			return false;
		}

		// From the disc if there is one, otherwise out of the executable (see Ps2Modules::Load())
		if (!Backends::Ps2Modules::Load("audsrv.irx"_s, "cdrom0:\\AUDSRV.IRX;1",
				Backends::Ps2Modules::Audsrv, Backends::Ps2Modules::AudsrvSize)) {
			LOGE("Cannot load \"audsrv.irx\", sound will be disabled");
			return false;
		}

		// `audsrv_init()` spins forever if the module failed its own init and registered no RPC server,
		// so bind with a cap first and disable sound instead of hanging at boot
		{
			constexpr std::int32_t BindAttempts = 100;
			constexpr std::int32_t BindPollIntervalMs = 10;

			SifRpcClientData_t probe;
			std::memset(&probe, 0, sizeof(probe));

			bool bound = false;
			for (std::int32_t attempt = 0; attempt < BindAttempts; attempt++) {
				if (SifBindRpc(&probe, AUDSRV_IRX, 0) >= 0 && probe.server != nullptr) {
					bound = true;
					break;
				}
				DelayThread(BindPollIntervalMs * 1000);
			}
			if (!bound) {
				LOGE("\"audsrv.irx\" is loaded but registered no RPC server, so it failed its own "
					"initialization - sound will be disabled");
				return false;
			}
		}

		const int error = audsrv_init();
		if (error != AUDSRV_ERR_NOERROR) {
			LOGE("audsrv_init() failed with error 0x{:.4x} ({}), sound will be disabled",
				std::uint32_t(error), audsrv_get_error_string());
			return false;
		}

		succeeded = true;
		return true;
	}

	Ps2AudioDevice::Ps2AudioDevice()
		: _valid(false), _stopReasons(0), _mixFrequency(DefaultMixingFrequency), _blockFrames(MaxBlockFrames),
			_ringCapacity(0), _buffers(nullptr), _bufferCount(0), _bufferCapacity(0), _residentBytes(0),
			_frameIndex(0), _idleSliceMs(2.0f), _mixBuffer(nullptr), _block(nullptr)
	{
		if (!InitializeModules()) {
			return;
		}

		// On failure every pointer must end up null: CreateBuffer() runs even on an invalid device, and only
		// a null `_buffers` stops it (a capacity of 0 would make it realloc to 0 bytes and write past them)
		_mixBuffer = static_cast<std::int32_t*>(std::malloc(std::size_t(MaxBlockFrames) * ChannelCount * sizeof(std::int32_t)));
		// The block is handed to the SIF transfer inside `audsrv_play_audio()`, so it starts on a cache line
		_block = static_cast<std::int16_t*>(memalign(64, std::size_t(MaxBlockFrames) * FrameBytes));
		_buffers = static_cast<Buffer*>(std::malloc(std::size_t(InitialBufferCapacity) * sizeof(Buffer)));
		if (_mixBuffer == nullptr || _block == nullptr || _buffers == nullptr) {
			LOGE("Cannot allocate the audio mixing buffers, sound will be disabled");
			ReleaseResources();
			return;
		}
		_bufferCapacity = InitialBufferCapacity;

		// Id 0 means "no buffer", so slot 0 is claimed before anything else can fail
		::new (&_buffers[0]) Buffer{};
		_bufferCount = 1;

		if (!ApplyFormat(_mixFrequency)) {
			ReleaseResources();
			return;
		}

		// Left at maximum, all gains are applied while mixing and attenuating twice would only cost resolution
		audsrv_set_volume(MAX_VOLUME);

		std::uint32_t sourceIds[MaxSources];
		for (std::int32_t i = 0; i < MaxSources; i++) {
			sourceIds[i] = std::uint32_t(i + 1);
		}
		SetSourcePool(arrayView(sourceIds, MaxSources));

		_valid = true;
		LOGI("Audio device initialized: audsrv on the SPU2, mixing {} Hz stereo in blocks of {} frames "
			"({} ms), {} bytes of ring ({} ms)", _mixFrequency, _blockFrames,
			(_blockFrames * 1000) / _mixFrequency, _ringCapacity,
			(_ringCapacity * 1000) / (_mixFrequency * FrameBytes));
	}

	Ps2AudioDevice::~Ps2AudioDevice()
	{
		// The decoding thread goes first (a no-op on this console, but the order is the contract)
		ShutdownDecodeThread();
		for (IdleDecode& decode : _idleDecodes) {
			decode.Request->CurrentState.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
		}
		_idleDecodes.clear();

		if (_valid) {
			// What the IOP still holds would keep playing for the length of the ring otherwise
			audsrv_stop_audio();
			_valid = false;
		}

		ReleaseResources();

		// audsrv stays resident, an IRX cannot be loaded twice (see InitializeModules())
	}

	void Ps2AudioDevice::ReleaseResources()
	{
		// Sample data is owned by raw pointers, so nothing releases it implicitly
		if (_buffers != nullptr) {
			for (std::int32_t i = 0; i < _bufferCount; i++) {
				ReleaseBuffer(_buffers[i]);
			}
			std::free(_buffers);
			_buffers = nullptr;
		}
		_bufferCount = 0;
		_bufferCapacity = 0;

		std::free(_mixBuffer);
		_mixBuffer = nullptr;
		std::free(_block);
		_block = nullptr;
	}

	bool Ps2AudioDevice::ApplyFormat(std::int32_t frequency)
	{
		audsrv_fmt_t format;
		format.freq = int(frequency);
		format.bits = 16;
		format.channels = ChannelCount;

		const int error = audsrv_set_format(&format);
		if (error != AUDSRV_ERR_NOERROR) {
			LOGE("audsrv_set_format({} Hz) failed with error 0x{:.4x} ({})",
				frequency, std::uint32_t(error), audsrv_get_error_string());
			return false;
		}

		_mixFrequency = frequency;
		// An empty ring reports its whole capacity, and it is empty right after the format is set
		const int available = audsrv_available();
		_ringCapacity = (available > 0 ? std::int32_t(available) : 0);

		// The largest block of which TargetBlocksPerRing still fit, rounded down to whole units
		std::int32_t blockFrames = (_ringCapacity / (TargetBlocksPerRing * FrameBytes) / BlockFramesUnit) * BlockFramesUnit;
		if (_ringCapacity <= 0) {
			// No answer means the measurement failed, not a tiny ring - the minimum block would starve the mixer
			blockFrames = MaxBlockFrames;
		} else if (blockFrames > MaxBlockFrames) {
			blockFrames = MaxBlockFrames;
		} else if (blockFrames < BlockFramesUnit) {
			// A real but small ring: a block bigger than the ring would silently never be submitted,
			// so use whatever whole units fit
			blockFrames = (_ringCapacity / (FrameBytes * BlockFramesUnit)) * BlockFramesUnit;
			if (blockFrames < BlockFramesUnit) {
				// Not even one unit fits, so fail loudly rather than stay silent with no cause
				LOGE("audsrv reported a {} byte ring, too small to submit {} frames into - sound will be disabled",
					_ringCapacity, BlockFramesUnit);
				return false;
			}
			LOGW("audsrv reported a {} byte ring, smaller than this backend expects - mixing in blocks of {} frames ({} ms)",
				_ringCapacity, blockFrames, (blockFrames * 1000) / frequency);
		}
		_blockFrames = blockFrames;
		return true;
	}

	bool Ps2AudioDevice::IsValid() const
	{
		return _valid;
	}

	const char* Ps2AudioDevice::GetName() const
	{
		return "audsrv";
	}

	void Ps2AudioDevice::SetGain(float gain)
	{
		// Applied while mixing rather than programmed into the module (see the constructor)
		_gain = gain;
	}

	void Ps2AudioDevice::UpdateListener(const Vector3f& position, const Vector3f& velocity)
	{
		// No Doppler on this backend, so the velocity is not kept
		static_cast<void>(velocity);
		_listenerPos = position;
	}

	std::int32_t Ps2AudioDevice::GetNativeFrequency()
	{
		return _mixFrequency;
	}

	void Ps2AudioDevice::SetMixingFrequency(std::int32_t frequency)
	{
		if (!_valid || frequency == _mixFrequency ||
			frequency < MinMixingFrequency || frequency > MaxMixingFrequency) {
			return;
		}

		if (_stopReasons != 0) {
			// Held stopped, so only record the rate; RestartStream() programs it when the last reason is dropped
			_mixFrequency = frequency;
			return;
		}

		const std::int32_t previousFrequency = _mixFrequency;

		// Drop audio mixed at the old rate, it would play at the wrong pitch. Through StopStream() so the
		// stop reasons stay truthful, otherwise RestartStream() would never reopen the ring.
		StopStream(StopReasonBlocking);
		if (!ReopenStream(frequency)) {
			// Refused, and the old format is gone with it, so restore the old rate (checked, not assumed)
			if (!ReopenStream(previousFrequency)) {
				// Both refused: the reason stays set, so the next EndBlockingOperation() or ResumeDevice() retries
				LOGE("Cannot reprogram the audio stream at {} Hz, nor back to {} Hz - sound is silent until "
					"the next level load", frequency, previousFrequency);
				return;
			}
			LOGW("audsrv refused {} Hz, the device stays at {} Hz", frequency, previousFrequency);
		}
		_stopReasons &= ~std::uint32_t(StopReasonBlocking);

		LOGI("Audio device now mixing at {} Hz in blocks of {} frames ({} bytes of ring, {} ms)",
			_mixFrequency, _blockFrames, _ringCapacity, (_ringCapacity * 1000) / (_mixFrequency * FrameBytes));
	}

	std::uint32_t Ps2AudioDevice::RegisterPlayer(IAudioPlayer* player)
	{
		const std::uint32_t sourceId = AudioDeviceBase::RegisterPlayer(player);
		if (sourceId != UnavailableSource) {
			if (Source* source = GetSource(sourceId)) {
				*source = Source{};
			}
		}
		return sourceId;
	}

	Ps2AudioDevice::Source* Ps2AudioDevice::GetSource(std::uint32_t sourceId)
	{
		if (sourceId == 0 || sourceId > std::uint32_t(MaxSources)) {
			return nullptr;
		}
		return &_sources[sourceId - 1];
	}

	std::uint32_t Ps2AudioDevice::CreateBuffer(BufferUsage usage)
	{
		// Every sound lives in main memory (SPU2 RAM belongs to audsrv), so the usage is irrelevant
		static_cast<void>(usage);

		if (_buffers == nullptr) {
			return 0;
		}

		for (std::int32_t i = 1; i < _bufferCount; i++) {
			if (!_buffers[i].Used) {
				// Samples are already released; the reset keeps a failed upload from reusing stale rate/channels
				_buffers[i] = Buffer{};
				_buffers[i].Used = true;
				return std::uint32_t(i);
			}
		}

		if (_bufferCount == _bufferCapacity) {
			// The old table stays valid on failure, and AudioBuffer survives 0, so a full heap only costs sounds
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

	void Ps2AudioDevice::ReleaseBuffer(Buffer& buffer)
	{
		// A source still reading this buffer would walk freed samples, so it is stopped first.
		// The id is range-checked because SetSourceBuffer() takes whatever it is handed.
		for (Source& source : _sources) {
			if (source.BufferId != 0 && source.BufferId < std::uint32_t(_bufferCount) && &_buffers[source.BufferId] == &buffer) {
				source.Playing = false;
				source.BufferId = 0;
			}
		}

		_residentBytes -= buffer.Capacity;
		std::free(buffer.Samples);
		buffer.Samples = nullptr;
		buffer.Capacity = 0;
		buffer.FrameCount = 0;
	}

	void Ps2AudioDevice::DeleteBuffer(std::uint32_t bufferId)
	{
		if (bufferId == 0 || bufferId >= std::uint32_t(_bufferCount)) {
			return;
		}
		ReleaseBuffer(_buffers[bufferId]);
		_buffers[bufferId].Used = false;
	}

	bool Ps2AudioDevice::UploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency)
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

		// `size` is in bytes, a trailing partial frame is dropped
		const std::int32_t frameSize = bytesPerSample * channelCount;
		const std::int32_t frameCount = size / frameSize;
		const std::int32_t byteCount = frameCount * frameSize;

		Buffer& buffer = _buffers[bufferId];
		if (byteCount <= 0) {
			ReleaseBuffer(buffer);
			return false;
		}

		// Streaming sources re-upload often, so an allocation that is big enough is kept
		if (byteCount > buffer.Capacity) {
			ReleaseBuffer(buffer);

			buffer.Samples = static_cast<std::uint8_t*>(std::malloc(std::size_t(byteCount)));
			if (buffer.Samples == nullptr) {
				LOGE("Cannot allocate {} bytes for audio buffer {} ({} bytes hold samples already), "
					"the sound will be silent", byteCount, bufferId, _residentBytes);
				return false;
			}
			buffer.Capacity = byteCount;
			_residentBytes += byteCount;
		}

		// Samples keep their width to save memory (most sounds are 8-bit); 8-bit is unsigned and gets
		// re-centered on 0, 16-bit is already native-endian
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

	void Ps2AudioDevice::SetSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->BufferId = bufferId;
			source->Cursor = 0;
		}
	}

	void Ps2AudioDevice::SetSourceGain(std::uint32_t sourceId, float gain)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Gain = gain;
		}
	}

	void Ps2AudioDevice::SetSourcePitch(std::uint32_t sourceId, float pitch)
	{
		if (Source* source = GetSource(sourceId)) {
			// A non-positive pitch would stall or reverse the cursor, neither of which the mixer expresses
			source->Pitch = (pitch > 0.0f ? pitch : 1.0f);
		}
	}

	void Ps2AudioDevice::SetSourceLooping(std::uint32_t sourceId, bool looping)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Looping = looping;
		}
	}

	void Ps2AudioDevice::SetSourceRelative(std::uint32_t sourceId, bool relative)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Relative = relative;
		}
	}

	void Ps2AudioDevice::SetSourcePosition(std::uint32_t sourceId, const Vector3f& position)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Position = position;
		}
	}

	void Ps2AudioDevice::SetSourceLowPass(std::uint32_t sourceId, float value)
	{
		// Not supported: audsrv has no per-source filter, and the mixer would pay for it on every source
		static_cast<void>(sourceId);
		static_cast<void>(value);
	}

	std::int32_t Ps2AudioDevice::GetSourceSampleOffset(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr ? std::int32_t(source->Cursor >> 32) : 0);
	}

	void Ps2AudioDevice::SetSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Cursor = std::int64_t(offset > 0 ? offset : 0) << 32;
		}
	}

	void Ps2AudioDevice::PlaySource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Playing = true;
			source->Paused = false;
		}
	}

	void Ps2AudioDevice::PauseSource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Paused = true;
		}
	}

	void Ps2AudioDevice::StopSource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Playing = false;
			source->Paused = false;
			source->Cursor = 0;
			// A stopped streaming source hands its queue back, which is what the player expects to collect
			for (std::int32_t i = 0; i < source->QueueCount && source->ProcessedCount < MaxQueuedBuffers; i++) {
				source->Processed[source->ProcessedCount++] = source->Queue[i];
			}
			source->QueueCount = 0;
		}
	}

	bool Ps2AudioDevice::IsSourcePlaying(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr && source->Playing && !source->Paused);
	}

	void Ps2AudioDevice::QueueBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
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

	std::int32_t Ps2AudioDevice::GetProcessedBufferCount(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr ? source->ProcessedCount : 0);
	}

	void Ps2AudioDevice::UnqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds)
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

	bool Ps2AudioDevice::ReopenStream(std::int32_t frequency)
	{
		// Programming the format is what reopens a stopped ring (see _stopReasons)
		if (!ApplyFormat(frequency)) {
			return false;
		}
		audsrv_set_volume(MAX_VOLUME);
		return true;
	}

	void Ps2AudioDevice::StopStream(StopReason reason)
	{
		if (!_valid) {
			return;
		}
		const std::uint32_t previous = _stopReasons;
		_stopReasons |= std::uint32_t(reason);
		if (previous != 0) {
			// Already stopped by the other owner, only record this reason
			return;
		}
		// Discard what the module holds, otherwise it would play over whatever comes next
		audsrv_stop_audio();
	}

	void Ps2AudioDevice::RestartStream(StopReason reason)
	{
		if (!_valid) {
			return;
		}
		_stopReasons &= ~std::uint32_t(reason);
		if (_stopReasons != 0) {
			// Another owner still wants it stopped
			return;
		}
		if (!ReopenStream(_mixFrequency)) {
			LOGE("Cannot restart the audio stream, sound will be silent");
			_valid = false;
			return;
		}
	}

	void Ps2AudioDevice::OnBlockingOperationBegan()
	{
		// UpdatePlayers() stops for longer than the ring holds, and an unfed module loops its last block
		StopStream(StopReasonBlocking);
	}

	void Ps2AudioDevice::OnBlockingOperationEnded()
	{
		RestartStream(StopReasonBlocking);
	}

	void Ps2AudioDevice::SuspendDevice()
	{
		StopStream(StopReasonSuspended);
	}

	void Ps2AudioDevice::ResumeDevice()
	{
		RestartStream(StopReasonSuspended);
	}

	void Ps2AudioDevice::UpdatePlayers()
	{
		// Advance chunks past their grace before the players run, so a finished one is queued this frame
		_frameIndex++;
		for (std::size_t i = 0; i < _idleDecodes.size(); ) {
			IdleDecode& decode = _idleDecodes[i];
			if (_frameIndex - decode.SubmittedFrame > IdleDecodeGraceFrames &&
				decode.Request->ExecuteSlice(InFrameDecodeSliceBytes)) {
				_idleDecodes.erase(&_idleDecodes[i]);
			} else {
				i++;
			}
		}

		// Players first, so the mix below sees this frame's state
		AudioDeviceBase::UpdatePlayers();

		if (_valid) {
			// FillRing() also handles suspension (see _stopReasons)
			FillRing();
		}
	}

	bool Ps2AudioDevice::SubmitStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request)
	{
		if (request == nullptr) {
			return false;
		}
		_idleDecodes.push_back(IdleDecode{request, _frameIndex});
		return true;
	}

	void Ps2AudioDevice::DrainStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request)
	{
		// Single-threaded, so a request is never mid-slice here and unlisting it is enough
		for (std::size_t i = 0; i < _idleDecodes.size(); i++) {
			if (_idleDecodes[i].Request == request) {
				request->CurrentState.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
				_idleDecodes.erase(&_idleDecodes[i]);
				return;
			}
		}
	}

	bool Ps2AudioDevice::RunIdleWork(float availableMs)
	{
		// The estimate is a slowly decaying maximum, so cheap slices cannot start an expensive one just before the blank
		if (_idleDecodes.empty() || availableMs < _idleSliceMs) {
			return false;
		}

		const TimeStamp start = TimeStamp::Now();
		if (_idleDecodes[0].Request->ExecuteSlice(IdleDecodeSliceBytes)) {
			_idleDecodes.erase(&_idleDecodes[0]);
		}
		const float elapsed = start.GetMillisecondsSince();
		_idleSliceMs = (elapsed > _idleSliceMs * 0.98f ? elapsed : _idleSliceMs * 0.98f);
		return !_idleDecodes.empty();
	}

	void Ps2AudioDevice::FillRing()
	{
		const std::int32_t blockBytes = _blockFrames * FrameBytes;

		if (_stopReasons != 0) {
			// The ring refuses everything until the stream is reopened (see StopStream())
			return;
		}

		// Submit only what fits instead of `audsrv_wait_audio()`, which blocks until the ring has room
		std::int32_t available = std::int32_t(audsrv_available());

		// Backstop against a ring that reports space it never consumes
		for (std::int32_t i = 0; i < MaxBlocksPerFill && available >= blockBytes; i++) {
			MixInto(_block, _blockFrames);

			const std::int32_t sent = std::int32_t(audsrv_play_audio(reinterpret_cast<const char*>(_block), blockBytes));
			if (sent <= 0) {
				// Refused outright, offering another block would not help
				break;
			}
			if (sent < blockBytes) {
				// Short accepts happen despite `available`; the rest is already mixed and lost (a click),
				// and `available` is now wrong, so let the next frame start clean
				LOGW("audsrv accepted only {} of {} bytes, {} frames of audio were dropped",
					sent, blockBytes, (blockBytes - sent) / FrameBytes);
				break;
			}
			available -= sent;
		}
	}

	Ps2AudioDevice::Buffer* Ps2AudioDevice::GetActiveBuffer(Source& source)
	{
		// A streaming source reads the head of its queue, a static one its single attached buffer
		const std::uint32_t bufferId = (source.QueueCount > 0 ? source.Queue[0] : source.BufferId);
		if (bufferId == 0 || bufferId >= std::uint32_t(_bufferCount)) {
			return nullptr;
		}
		Buffer& buffer = _buffers[bufferId];
		return (buffer.Used && buffer.Samples != nullptr && buffer.FrameCount > 0 ? &buffer : nullptr);
	}

	namespace
	{
		/**
			@brief Mixes the frames one buffer still holds into @p output, from frame @p i until either runs out

			A template per sample format, so the loop has no per-sample format tests and keeps the cursor and
			gains in registers (without strict aliasing, a member cursor would be reloaded on every store).

			@returns The index of the first output frame not mixed, which is @p frames unless the buffer ran out
		*/
		template<std::int32_t BytesPerSample, std::int32_t Channels>
		std::int32_t MixRun(const std::uint8_t* data, std::int32_t frameCount, std::int32_t* output, std::int32_t i,
			std::int32_t frames, std::int64_t& cursorInOut, std::int64_t step, std::int64_t end,
			std::int32_t leftQ15, std::int32_t rightQ15)
		{
			using Sample = typename std::conditional<BytesPerSample == 1, std::int8_t, std::int16_t>::type;
			// 8-bit samples are widened here, one shift each instead of storing them at twice the size
			constexpr std::int32_t Shift = (BytesPerSample == 1 ? 8 : 0);
			const Sample* samples = reinterpret_cast<const Sample*>(data);
			std::int64_t cursor = cursorInOut;
			for (; i < frames && cursor < end; i++) {
				// Linear interpolation with a Q8 fraction; the upper neighbor is clamped, wrapping would click
				const std::int32_t index = std::int32_t(cursor >> 32);
				const std::int32_t fraction = std::int32_t((cursor >> 24) & 0xFF);
				const std::int32_t nextIndex = (index + 1 < frameCount ? index + 1 : index);

				const std::int32_t l0 = std::int32_t(samples[index * Channels]) << Shift;
				const std::int32_t r0 = (Channels == 2 ? std::int32_t(samples[index * Channels + 1]) << Shift : l0);
				const std::int32_t l1 = std::int32_t(samples[nextIndex * Channels]) << Shift;
				const std::int32_t r1 = (Channels == 2 ? std::int32_t(samples[nextIndex * Channels + 1]) << Shift : l1);
				const std::int32_t left = l0 + (((l1 - l0) * fraction) >> 8);
				const std::int32_t right = r0 + (((r1 - r0) * fraction) >> 8);

				// The accumulator is stereo, as the stream is (see FrameBytes)
				output[i * 2] += (left * leftQ15) >> 15;
				output[i * 2 + 1] += (right * rightQ15) >> 15;
				cursor += step;
			}
			cursorInOut = cursor;
			return i;
		}
	}

	void Ps2AudioDevice::ComputePanning(const Source& source, float& leftGain, float& rightGain) const
	{
		// The positional model is shared with the other software-mixing backends, so it sounds the same
		AudioMixer::ComputeStereoGains(source.Relative, source.Position, _listenerPos, source.Gain, _gain, leftGain, rightGain);
	}

	bool Ps2AudioDevice::MixSource(Source& source, std::int32_t* output, std::int32_t frames)
	{
		Buffer* buffer = GetActiveBuffer(source);
		if (buffer == nullptr) {
			return false;
		}

		// Q15 gains keep the per-sample loop in integers; both are at most 1.0, so products fit in 31 bits
		float leftGain, rightGain;
		ComputePanning(source, leftGain, rightGain);
		const std::int32_t leftQ15 = std::int32_t(leftGain * 32768.0f + 0.5f);
		const std::int32_t rightQ15 = std::int32_t(rightGain * 32768.0f + 0.5f);

		// Cursor advance per output frame in 32.32, covering both pitch and the buffer/stream rate difference
		std::int64_t step = AudioMixer::ComputeResampleStep(buffer->Frequency, _mixFrequency, source.Pitch);
		std::int64_t end = std::int64_t(buffer->FrameCount) << 32;

		// The cursor is a local for the whole block and goes back into the source on the way out
		std::int64_t cursor = source.Cursor;
		std::int32_t i = 0;
		while (i < frames) {
			while (cursor >= end) {
				if (source.QueueCount > 0) {
					// A streaming source moves on to the next queued buffer, handing the exhausted one back
					if (source.ProcessedCount < MaxQueuedBuffers) {
						source.Processed[source.ProcessedCount++] = source.Queue[0];
					}
					for (std::int32_t q = 1; q < source.QueueCount; q++) {
						source.Queue[q - 1] = source.Queue[q];
					}
					source.QueueCount--;
					cursor -= end;
					buffer = GetActiveBuffer(source);
					if (buffer == nullptr) {
						source.Cursor = cursor;
						return false;
					}
					// The next buffer may have another rate; re-test instead of skipping a frame, so no gap
					step = AudioMixer::ComputeResampleStep(buffer->Frequency, _mixFrequency, source.Pitch);
					end = std::int64_t(buffer->FrameCount) << 32;
				} else if (source.Looping) {
					// Wrapped, not reset, to keep the overshoot fraction (`end` is never zero here)
					cursor %= end;
				} else {
					source.Cursor = cursor;
					return false;
				}
			}

			const std::uint8_t* data = buffer->Samples;
			const std::int32_t frameCount = buffer->FrameCount;
			if (buffer->BytesPerSample == 1) {
				i = (buffer->ChannelCount == 2
					? MixRun<1, 2>(data, frameCount, output, i, frames, cursor, step, end, leftQ15, rightQ15)
					: MixRun<1, 1>(data, frameCount, output, i, frames, cursor, step, end, leftQ15, rightQ15));
			} else {
				i = (buffer->ChannelCount == 2
					? MixRun<2, 2>(data, frameCount, output, i, frames, cursor, step, end, leftQ15, rightQ15)
					: MixRun<2, 1>(data, frameCount, output, i, frames, cursor, step, end, leftQ15, rightQ15));
			}
		}
		source.Cursor = cursor;
		return true;
	}

	void Ps2AudioDevice::MixInto(std::int16_t* output, std::int32_t frames)
	{
		// Silence is common, so write it straight out without the accumulator
		bool anyPlaying = false;
		for (const Source& source : _sources) {
			if (source.Playing && !source.Paused) {
				anyPlaying = true;
				break;
			}
		}
		if (!anyPlaying) {
			std::memset(output, 0, std::size_t(frames) * FrameBytes);
			return;
		}

		std::int32_t* accumulator = _mixBuffer;
		std::memset(accumulator, 0, std::size_t(frames) * ChannelCount * sizeof(std::int32_t));

		for (Source& source : _sources) {
			if (!source.Playing || source.Paused) {
				continue;
			}
			if (!MixSource(source, accumulator, frames)) {
				// Finished or starved, the player notices through IsSourcePlaying()
				source.Playing = false;
				source.Cursor = 0;
			}
		}

		// Clamp rather than wrap, wrapping would flip the polarity
		const std::int32_t total = frames * ChannelCount;
		for (std::int32_t i = 0; i < total; i++) {
			output[i] = AudioMixer::ClampToInt16(accumulator[i]);
		}
	}
}

#endif
