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
		// The device is constructed and destroyed with the service locator, which the application may do more
		// than once, but an IRX can only be loaded once - so this runs at most once per process and remembers
		// what it found. Everything it does is on the IOP side; the EE side (the format, the ring) belongs to
		// the instance and is set up in the constructor.
		static bool attempted = false;
		static bool succeeded = false;
		if (attempted) {
			return succeeded;
		}
		attempted = true;

		// audsrv drives the SPU2 through `libsd`, the ROM module that owns the sound registers, so that goes
		// first; audsrv itself is carried inside the executable rather than read from the disc, because the
		// game also boots from an SD card where there is no disc to read it from (see Ps2Modules.h)
		const int libsdId = SifLoadModule("rom0:LIBSD", 0, nullptr);
		if (libsdId < 0) {
			LOGE("SifLoadModule(\"rom0:LIBSD\") failed with error {}, sound will be disabled", libsdId);
			return false;
		}

		if (!Backends::Ps2Modules::Load("audsrv.irx"_s, Backends::Ps2Modules::Audsrv, Backends::Ps2Modules::AudsrvSize)) {
			LOGE("Cannot load \"audsrv.irx\", sound will be disabled");
			return false;
		}

		// `Ps2Modules::Load()` cannot tell an already-resident copy from one that ran and failed - both leave
		// `_start()` with MODULE_NO_RESIDENT_END - and `audsrv_init()` binds to the module's RPC server in a
		// loop that NEVER ENDS if nothing registered one. On a module that failed its own initialization
		// (no memory for its ring, LIBSD absent) that is a black screen at boot with nothing logged, which
		// is unrecoverable rather than merely silent. So the bind audsrv would spin on is done here first,
		// with a cap: if the server is not there, the module is not serving and sound is disabled instead.
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
			_mixBuffer(nullptr), _block(nullptr)
	{
		if (!InitializeModules()) {
			return;
		}

		// Each allocation is released again if a later one fails, so a device that gives up here is left
		// with every pointer still null. Nothing gates createBuffer() on isValid() - AudioBuffer's
		// constructor calls it regardless, and this device is registered either way - so `_buffers` being
		// null is what tells it there is no table, and a half-built one would be worse than none: with
		// `_bufferCapacity` still 0 its growth step computes a new capacity of 0, hands that to
		// `std::realloc()` (which on this newlib shrinks the block and returns it rather than freeing it)
		// and then constructs a Buffer into the few bytes that came back.
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

		// Buffer id 0 is reserved as "no buffer", so the table starts with an unused entry. It is claimed
		// BEFORE anything else can fail, because a table whose slot 0 is free would hand that id out as an
		// ordinary buffer - and every caller reads 0 back as failure.
		::new (&_buffers[0]) Buffer{};
		_bufferCount = 1;

		if (!ApplyFormat(_mixFrequency)) {
			ReleaseResources();
			return;
		}

		// The module's own volume is left at the maximum: everything the game controls - the master volume,
		// per-player gains, the positional attenuation - is already applied while mixing, and halving it
		// twice would only cost resolution in the 16-bit samples the SPU2 receives
		audsrv_set_volume(MAX_VOLUME);

		std::uint32_t sourceIds[MaxSources];
		for (std::int32_t i = 0; i < MaxSources; i++) {
			sourceIds[i] = std::uint32_t(i + 1);
		}
		setSourcePool(arrayView(sourceIds, MaxSources));

		_valid = true;
		LOGI("Audio device initialized: audsrv on the SPU2, mixing {} Hz stereo in blocks of {} frames "
			"({} ms), {} bytes of ring ({} ms)", _mixFrequency, _blockFrames,
			(_blockFrames * 1000) / _mixFrequency, _ringCapacity,
			(_ringCapacity * 1000) / (_mixFrequency * FrameBytes));
	}

	Ps2AudioDevice::~Ps2AudioDevice()
	{
		// The base class's decoding thread can still be handing buffers over, so it goes first (a no-op on
		// this console, where the engine's threading is off, but the order is the contract)
		shutdownDecodeThread();

		if (_valid) {
			// What the IOP still holds would keep playing for the length of the ring otherwise
			audsrv_stop_audio();
			_valid = false;
		}

		ReleaseResources();

		// audsrv itself is NOT shut down: the module stays resident for the life of the process (an IRX
		// cannot be loaded a second time), and `InitializeModules()` above is what remembers that
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
		// An empty ring answers with its whole capacity, which is the only way to learn what this build of
		// the module was compiled with (and the ring IS empty here - the format was just reprogrammed)
		const int available = audsrv_available();
		_ringCapacity = (available > 0 ? std::int32_t(available) : 0);

		// The largest block of which TargetBlocksPerRing still fit, rounded down to a whole number of units
		// so the figure stays a tidy one in the log.
		std::int32_t blockFrames = (_ringCapacity / (TargetBlocksPerRing * FrameBytes) / BlockFramesUnit) * BlockFramesUnit;
		if (_ringCapacity <= 0) {
			// The module answered with nothing at all, which means the measurement is unusable rather than
			// that the ring is tiny - so the MAXIMUM goes in, not the minimum. Clamping down here instead
			// would pick the smallest block this backend can submit: at 64 frames, `FillRing()` can queue at
			// most MaxBlocksPerFill of them per call, which is under 23 ms of audio against a frame that can
			// easily run 33 - a mixer permanently starved by the recovery path, and paying eight SIF round
			// trips a frame to do it.
			blockFrames = MaxBlockFrames;
		} else if (blockFrames > MaxBlockFrames) {
			blockFrames = MaxBlockFrames;
		} else if (blockFrames < BlockFramesUnit) {
			// A ring that measured smaller than TargetBlocksPerRing blocks of one unit is a real answer, not
			// a failed measurement, and the maximum must NOT be assumed for it: `FillRing()` only submits
			// while `audsrv_available() >= blockBytes`, so a block bigger than the whole ring is a gate that
			// can never open - no samples, no log, no `break`, and isValid() still true. Whatever whole
			// units do fit is used instead; a starved mixer still plays.
			blockFrames = (_ringCapacity / (FrameBytes * BlockFramesUnit)) * BlockFramesUnit;
			if (blockFrames < BlockFramesUnit) {
				// Under one unit the ring cannot hold a submission at all, so there is nothing to fall back
				// to - said plainly here rather than left to look like silence with no cause
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

	bool Ps2AudioDevice::isValid() const
	{
		return _valid;
	}

	const char* Ps2AudioDevice::name() const
	{
		return "audsrv";
	}

	void Ps2AudioDevice::setGain(float gain)
	{
		// Applied while mixing rather than programmed into the module (see the constructor)
		_gain = gain;
	}

	void Ps2AudioDevice::updateListener(const Vector3f& position, const Vector3f& velocity)
	{
		// No Doppler on this backend, so the velocity is not kept
		static_cast<void>(velocity);
		_listenerPos = position;
	}

	std::int32_t Ps2AudioDevice::nativeFrequency()
	{
		return _mixFrequency;
	}

	void Ps2AudioDevice::setMixingFrequency(std::int32_t frequency)
	{
		if (!_valid || frequency == _mixFrequency ||
			frequency < MinMixingFrequency || frequency > MaxMixingFrequency) {
			return;
		}

		if (_stopReasons != 0) {
			// Held stopped by a blocking operation or a suspension, and reopening the ring behind either of
			// those is exactly what the stop reasons exist to prevent. Recording the rate is enough:
			// RestartStream() programs `_mixFrequency` when the last reason is dropped.
			_mixFrequency = frequency;
			return;
		}

		const std::int32_t previousFrequency = _mixFrequency;

		// Everything already queued was mixed at the old rate and would be played back at the new one, so
		// the ring is dropped rather than left to play a burst at the wrong pitch. ReopenStream() below is
		// also what reopens it - a stopped ring accepts nothing until then.
		//
		// Through StopStream() rather than by calling `audsrv_stop_audio()` directly: the ring really is
		// closed here, and a state that says otherwise is one nothing can recover from. It used to leave
		// the stream marked open while the module was refusing everything, so RestartStream() - the one
		// function whose job is to reopen it - returned immediately and the device was silent for good.
		StopStream(StopReasonBlocking);
		if (!ReopenStream(frequency)) {
			// The module refused the new rate and the old format is gone with it, so the old one is put
			// back - it was accepted once, so it should be again, but that is checked rather than assumed
			if (!ReopenStream(previousFrequency)) {
				// Both refused. The reason is deliberately left in place: the stream stays marked stopped,
				// which is the truth, and the next endBlockingOperation() or resumeDevice() retries it
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

	std::uint32_t Ps2AudioDevice::registerPlayer(IAudioPlayer* player)
	{
		const std::uint32_t sourceId = AudioDeviceBase::registerPlayer(player);
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

	std::uint32_t Ps2AudioDevice::createBuffer(BufferUsage usage)
	{
		// Every sound lives in main memory here - the SPU2's own 2 MB is owned by audsrv's streaming voices -
		// so the usage says nothing this backend can act on
		static_cast<void>(usage);

		if (_buffers == nullptr) {
			return 0;
		}

		for (std::int32_t i = 1; i < _bufferCount; i++) {
			if (!_buffers[i].Used) {
				// A free slot has already had its samples released by deleteBuffer(), so resetting the
				// descriptor drops nothing - it only keeps the previous sound's rate and channel count from
				// being read if the next upload fails before it sets its own
				_buffers[i] = Buffer{};
				_buffers[i].Used = true;
				return std::uint32_t(i);
			}
		}

		if (_bufferCount == _bufferCapacity) {
			// The old table stays valid until the new one exists, which is the whole point of doing this by
			// hand. Returning 0 is the interface's "no buffer" and AudioBuffer already reports and survives
			// it, so a full heap costs sounds rather than the game.
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
		// The id is range-checked because setSourceBuffer() takes whatever it is handed.
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

	void Ps2AudioDevice::deleteBuffer(std::uint32_t bufferId)
	{
		if (bufferId == 0 || bufferId >= std::uint32_t(_bufferCount)) {
			return;
		}
		ReleaseBuffer(_buffers[bufferId]);
		_buffers[bufferId].Used = false;
	}

	bool Ps2AudioDevice::uploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency)
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

		// `size` is a byte count on every path into here, so a trailing partial frame is dropped rather than
		// turned into a sample count that disagrees with the data
		const std::int32_t frameSize = bytesPerSample * channelCount;
		const std::int32_t frameCount = size / frameSize;
		const std::int32_t byteCount = frameCount * frameSize;

		Buffer& buffer = _buffers[bufferId];
		if (byteCount <= 0) {
			ReleaseBuffer(buffer);
			return false;
		}

		// A streaming source uploads into the same buffer several times a second, so an allocation that is
		// already big enough is kept rather than churned
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

		// Samples keep the width they arrived in. The mixer works in 16 bits and widening here would save it
		// one shift per sample, but it would also double what this console's content costs to hold - nearly
		// all of the game's sounds are 8-bit - on a machine whose 32 MB is shared with a renderer that pages
		// every texture through main memory. The 8-bit form is unsigned with 128 at silence, which is the one
		// conversion left to make; 16-bit data is already native-endian (the asset readers swap on load).
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

	void Ps2AudioDevice::setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->BufferId = bufferId;
			source->Cursor = 0;
		}
	}

	void Ps2AudioDevice::setSourceGain(std::uint32_t sourceId, float gain)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Gain = gain;
		}
	}

	void Ps2AudioDevice::setSourcePitch(std::uint32_t sourceId, float pitch)
	{
		if (Source* source = GetSource(sourceId)) {
			// A non-positive pitch would stall or reverse the cursor, neither of which the mixer expresses
			source->Pitch = (pitch > 0.0f ? pitch : 1.0f);
		}
	}

	void Ps2AudioDevice::setSourceLooping(std::uint32_t sourceId, bool looping)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Looping = looping;
		}
	}

	void Ps2AudioDevice::setSourceRelative(std::uint32_t sourceId, bool relative)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Relative = relative;
		}
	}

	void Ps2AudioDevice::setSourcePosition(std::uint32_t sourceId, const Vector3f& position)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Position = position;
		}
	}

	void Ps2AudioDevice::setSourceLowPass(std::uint32_t sourceId, float value)
	{
		// audsrv streams through two plain voices and exposes nothing per-source, and filtering in the mixer
		// would cost every source a per-sample branch for the single effect that asks for it
		static_cast<void>(sourceId);
		static_cast<void>(value);
	}

	std::int32_t Ps2AudioDevice::sourceSampleOffset(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr ? std::int32_t(source->Cursor >> 32) : 0);
	}

	void Ps2AudioDevice::setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Cursor = std::int64_t(offset > 0 ? offset : 0) << 32;
		}
	}

	void Ps2AudioDevice::playSource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Playing = true;
			source->Paused = false;
		}
	}

	void Ps2AudioDevice::pauseSource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Paused = true;
		}
	}

	void Ps2AudioDevice::stopSource(std::uint32_t sourceId)
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

	bool Ps2AudioDevice::isSourcePlaying(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr && source->Playing && !source->Paused);
	}

	void Ps2AudioDevice::queueBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
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

	std::int32_t Ps2AudioDevice::numProcessedBuffers(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr ? source->ProcessedCount : 0);
	}

	void Ps2AudioDevice::unqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds)
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
		// Programming the format is what reopens the ring; without it `audsrv_available()` answers 0 for the
		// rest of the session and FillRing() below would never submit anything again (see _stopReasons)
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
			// Already held stopped by the other owner, so the module needs nothing - this only records that
			// it must stay stopped until this reason is dropped as well
			return;
		}
		// What the module still holds is discarded rather than left to drain - it is the scene that is being
		// left, and it would otherwise play over whatever comes next
		audsrv_stop_audio();
	}

	void Ps2AudioDevice::RestartStream(StopReason reason)
	{
		if (!_valid) {
			return;
		}
		_stopReasons &= ~std::uint32_t(reason);
		if (_stopReasons != 0) {
			// Somebody else still wants it stopped - reopening here is what used to let a level load's
			// endBlockingOperation() undo a suspension that was still in force
			return;
		}
		if (!ReopenStream(_mixFrequency)) {
			LOGE("Cannot restart the audio stream, sound will be silent");
			_valid = false;
			return;
		}
	}

	void Ps2AudioDevice::onBlockingOperationBegan()
	{
		// The caller is about to stop calling updatePlayers() for far longer than the ring holds. Left alone,
		// the module would spend that whole time repeating the last block it was given - a fragment of the
		// music, over and over, for as long as the load takes (measured; fed silence, by contrast, comes out
		// as silence, so this is the module repeating rather than the hardware idling). Stopping it turns
		// that into the silence it should have been.
		StopStream(StopReasonBlocking);
	}

	void Ps2AudioDevice::onBlockingOperationEnded()
	{
		RestartStream(StopReasonBlocking);
	}

	void Ps2AudioDevice::suspendDevice()
	{
		StopStream(StopReasonSuspended);
	}

	void Ps2AudioDevice::resumeDevice()
	{
		RestartStream(StopReasonSuspended);
	}

	void Ps2AudioDevice::updatePlayers()
	{
		// The base class advances the players and retires the finished ones first, so the mix below sees the
		// state this frame actually asked for
		AudioDeviceBase::updatePlayers();

		if (_valid) {
			// A suspension is one of the reasons FillRing() finds the stream stopped, so it needs no test
			// of its own here (see _stopReasons)
			FillRing();
		}
	}

	void Ps2AudioDevice::FillRing()
	{
		const std::int32_t blockBytes = _blockFrames * FrameBytes;

		if (_stopReasons != 0) {
			// The ring refuses everything until the format is programmed again, so there is nothing to do
			// here until everyone who stopped the stream has put it back (see StopStream())
			return;
		}

		// `audsrv_wait_audio()` is what the module's own samples are written around, and it BLOCKS until the
		// ring has room - which on a frame that already ran long would stall the game on the SPU2. Asking how
		// much fits and submitting only that is the non-blocking form of the same thing.
		std::int32_t available = std::int32_t(audsrv_available());

		// The count is capped only as a backstop. Filling this ring completely is around fifty milliseconds of
		// audio, which is cheap enough to mix inside one frame - unlike the consoles whose queue holds three
		// times that, where an unbounded catch-up turns a late frame into a much later one. What the cap
		// really rules out is a ring that reports space it never consumes, which would otherwise spin here.
		for (std::int32_t i = 0; i < MaxBlocksPerFill && available >= blockBytes; i++) {
			MixInto(_block, _blockFrames);

			const std::int32_t sent = std::int32_t(audsrv_play_audio(reinterpret_cast<const char*>(_block), blockBytes));
			if (sent <= 0) {
				// The module refused the block outright, so there is nothing to be gained by offering another
				break;
			}
			if (sent < blockBytes) {
				// A SHORT accept. The EE side of `audsrv_play_audio()` chunks the block through its own RPC
				// buffer and returns how much the IOP took, breaking out of that loop if a chunk is refused
				// - so this is a value the module can genuinely return, whatever the space measured above
				// said. It costs samples rather than just latency: `MixInto()` has already walked every
				// source's cursor across the WHOLE block, so the part that was not taken is gone and is
				// heard as a click. Nothing here can put it back, but continuing would compound it against
				// an `available` that is now wrong, so the fill ends and the next frame starts clean.
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

	// Always inlined: this is called twice per OUTPUT SAMPLE by the resampler below, tens of thousands of
	// times a frame, and out of line it costs a call, two stores through the reference parameters and a
	// re-test of the format on every one of them - all three are hoisted once it is inlined, because the
	// format and the channel count are loop invariants of MixSource
	DEATH_ALWAYS_INLINE void Ps2AudioDevice::ReadFrame(const Buffer& buffer, std::int32_t frame, std::int32_t& left, std::int32_t& right)
	{
		const std::int32_t index = frame * buffer.ChannelCount;
		if (buffer.BytesPerSample == 1) {
			// An 8-bit source is stored as it arrived and shifted into the mixer's scale here: one
			// instruction per sample against holding the whole sound at twice the size
			const std::int8_t* samples = reinterpret_cast<const std::int8_t*>(buffer.Samples);
			left = std::int32_t(samples[index]) << 8;
			right = (buffer.ChannelCount == 2 ? std::int32_t(samples[index + 1]) << 8 : left);
		} else {
			const std::int16_t* samples = reinterpret_cast<const std::int16_t*>(buffer.Samples);
			left = samples[index];
			right = (buffer.ChannelCount == 2 ? samples[index + 1] : left);
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

		// The gains are computed in float - the same math as every other backend, so panning sounds identical
		// - then held in Q15 for the block, keeping the per-sample loop entirely in integer registers. Both
		// are at most 1.0, so a Q15 gain times a 16-bit sample stays inside 31 bits.
		float leftGain, rightGain;
		ComputePanning(source, leftGain, rightGain);
		const std::int32_t leftQ15 = std::int32_t(leftGain * 32768.0f + 0.5f);
		const std::int32_t rightQ15 = std::int32_t(rightGain * 32768.0f + 0.5f);

		// One output frame advances the cursor by this much of an input frame (in the cursor's 32.32 fixed
		// point), which is where both the source's pitch and the rate difference between the buffer and the
		// stream are applied. Computed once per block and per buffer - the per-sample loop never touches the
		// FPU, which on the R5900 also keeps it out of the way of everything else that does.
		std::int64_t step = AudioMixer::ComputeResampleStep(buffer->Frequency, _mixFrequency, source.Pitch);
		std::int64_t end = std::int64_t(buffer->FrameCount) << 32;

		for (std::int32_t i = 0; i < frames; i++) {
			while (source.Cursor >= end) {
				if (source.QueueCount > 0) {
					// A streaming source moves on to the next queued buffer, handing the exhausted one back
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
					// The next buffer may be at another rate, so the step follows it - and the loop re-tests
					// rather than skipping this output frame, so a buffer boundary costs no gap
					step = AudioMixer::ComputeResampleStep(buffer->Frequency, _mixFrequency, source.Pitch);
					end = std::int64_t(buffer->FrameCount) << 32;
				} else if (source.Looping) {
					// Wrapped rather than reset, so a step that overshoots the end does not lose the fraction
					// of a frame it went past by - over a long loop that would drift audibly
					// (GetActiveBuffer() guarantees FrameCount > 0, so `end` is never zero here)
					source.Cursor %= end;
				} else {
					return false;
				}
			}

			// Linear interpolation between the two frames the cursor sits between, with the fraction held in
			// Q8 - plenty for 16-bit samples. The upper neighbour is clamped to the last frame rather than
			// wrapped: at the very end of a non-looping buffer there is nothing after it, and wrapping would
			// fold the first sample into the last one as a click.
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

	void Ps2AudioDevice::MixInto(std::int16_t* output, std::int32_t frames)
	{
		// Silence is the common case - a menu with the music off, and most frames of a level between effects -
		// and it does not need the accumulator at all. Zeroing 32 bits per channel and then converting every
		// one of them back down to 16 is twice the memory traffic of writing the silence straight out, and
		// the ring is topped up with a block or two of it every single frame.
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
				// Ran out of audio: a static source has finished, and a streaming one has been starved by a
				// decoder that could not keep up. Both stop; the player notices through isSourcePlaying().
				source.Playing = false;
				source.Cursor = 0;
			}
		}

		// The stream carries 16 bits and wrapping the excess would flip its polarity, a far worse artefact
		// than the clipping this is instead. With this many voices a loud moment can exceed the range, which
		// is the whole reason the accumulator is wider than the output.
		const std::int32_t total = frames * ChannelCount;
		for (std::int32_t i = 0; i < total; i++) {
			output[i] = AudioMixer::ClampToInt16(accumulator[i]);
		}
	}
}

#endif
