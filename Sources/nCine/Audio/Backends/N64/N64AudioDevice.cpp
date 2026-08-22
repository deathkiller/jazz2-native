#if defined(WITH_N64AUDIO)

#include "N64AudioDevice.h"
#include "../../AudioMixerCommon.h"
#include "../../IAudioPlayer.h"
#include "../../../../Main.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

#include <audio.h>
#include <n64sys.h>

namespace nCine
{
	namespace
	{
		/** @brief Bytes of heap still free, which is what every allocation here is judged against */
		std::int32_t GetFreeHeapBytes()
		{
			heap_stats_t stats;
			sys_get_heap_stats(&stats);
			return stats.free;
		}
	}

	N64AudioDevice::N64AudioDevice()
		: _valid(false), _suspended(false), _outputFrequency(OutputFrequency), _bufferFrames(0),
			_residentBytes(0), _residentLogged(0), _buffers(nullptr), _bufferCount(0), _bufferCapacity(0),
			_mixBuffer(nullptr)
	{
		// The second argument is a headroom, not a buffer count, and it sizes the whole queue. With no
		// mixer thread the queue is topped up once per frame from updatePlayers() (see the class
		// documentation), so it has to outlive a late frame; the default works out to eight buffers of
		// 960 frames, about 160 ms, which is ten 16.6 ms frames of slack - twice what the PS3 backend's
		// 85 ms ring allows itself. libdragon sizes and aligns the buffers itself, which matters
		// here: the AI DMAs straight out of RDRAM and wants 8-byte aligned, even-length transfers, and
		// audio_write_begin() hands those buffers out directly so the mix lands in DMA-able memory
		// without a copy.
		audio_init(OutputFrequency, AUDIO_DEFAULT_LATENCY);

		// The AI clocks itself off the video clock through an integer divider, so the granted rate is
		// only near the requested one (and differs between NTSC and PAL units); every cursor step and
		// every stream decode uses the granted rate, or long audio would drift against the clock
		_outputFrequency = audio_get_frequency();
		_bufferFrames = audio_get_buffer_length();
		if (_outputFrequency <= 0 || _bufferFrames <= 0) {
			LOGE("Cannot initialize the audio interface, sound will be disabled");
			audio_close();
			return;
		}

		const std::int32_t mixBufferBytes = _bufferFrames * ChannelCount * std::int32_t(sizeof(std::int32_t));
		_mixBuffer = static_cast<std::int32_t*>(std::malloc(std::size_t(mixBufferBytes)));
		if (_mixBuffer == nullptr) {
			LOGE("Cannot allocate {} bytes for the audio mix buffer ({} bytes of heap free), sound will be disabled",
				mixBufferBytes, GetFreeHeapBytes());
			audio_close();
			return;
		}

		_buffers = static_cast<Buffer*>(std::malloc(std::size_t(InitialBufferCapacity) * sizeof(Buffer)));
		if (_buffers == nullptr) {
			LOGE("Cannot allocate the audio buffer table ({} bytes of heap free), sound will be disabled",
				GetFreeHeapBytes());
			std::free(_mixBuffer);
			_mixBuffer = nullptr;
			audio_close();
			return;
		}
		_bufferCapacity = InitialBufferCapacity;

		// Buffer id 0 is reserved as "no buffer", so the table starts with an unused entry
		::new (&_buffers[0]) Buffer{};
		_bufferCount = 1;

		std::uint32_t sourceIds[MaxSources];
		for (std::int32_t i = 0; i < MaxSources; i++) {
			sourceIds[i] = std::uint32_t(i + 1);
		}
		setSourcePool(arrayView(sourceIds, MaxSources));

		_valid = true;
		LOGI("Audio device initialized: libdragon AI, {} Hz stereo, {} buffers of {} samples, {} bytes of heap free",
			_outputFrequency, audio_get_num_buffers(), _bufferFrames, GetFreeHeapBytes());
	}

	N64AudioDevice::~N64AudioDevice()
	{
		// The base class's decoding thread can still be handing buffers over, so it goes first
		shutdownDecodeThread();

		if (_valid) {
			audio_close();
			_valid = false;
		}

		// Sample data is owned by raw pointers, so nothing releases it implicitly
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

	bool N64AudioDevice::isValid() const
	{
		return _valid;
	}

	const char* N64AudioDevice::name() const
	{
		return "libdragon AI";
	}

	void N64AudioDevice::setGain(float gain)
	{
		// Applied while mixing rather than programmed anywhere: the AI has no volume register at all
		_gain = gain;
	}

	void N64AudioDevice::updateListener(const Vector3f& position, const Vector3f& velocity)
	{
		// No Doppler on this backend, so the velocity is not kept
		static_cast<void>(velocity);
		_listenerPos = position;
	}

	std::int32_t N64AudioDevice::nativeFrequency()
	{
		return _outputFrequency;
	}

	std::uint32_t N64AudioDevice::registerPlayer(IAudioPlayer* player)
	{
		const std::uint32_t sourceId = AudioDeviceBase::registerPlayer(player);
		if (sourceId != UnavailableSource) {
			if (Source* source = GetSource(sourceId)) {
				*source = Source{};
			}
		}
		return sourceId;
	}

	N64AudioDevice::Source* N64AudioDevice::GetSource(std::uint32_t sourceId)
	{
		if (sourceId == 0 || sourceId > std::uint32_t(MaxSources)) {
			return nullptr;
		}
		return &_sources[sourceId - 1];
	}

	std::uint32_t N64AudioDevice::createBuffer(BufferUsage usage)
	{
		// Both kinds live in RDRAM here - there is no separate sound RAM to place them in - so the
		// usage says nothing this backend can act on
		static_cast<void>(usage);

		if (_buffers == nullptr) {
			return 0;
		}

		for (std::int32_t i = 1; i < _bufferCount; i++) {
			if (!_buffers[i].Used) {
				// A free slot has already had its samples released by deleteBuffer(), so resetting the
				// descriptor drops nothing - it only keeps the previous sound's rate and channel count
				// from being read if the next upload fails before it sets its own
				_buffers[i] = Buffer{};
				_buffers[i].Used = true;
				return std::uint32_t(i);
			}
		}

		if (_bufferCount == _bufferCapacity) {
			// The old table stays valid until the new one exists, which is the whole point of doing
			// this by hand. Returning 0 is the interface's "no buffer" and AudioBuffer already
			// reports and survives it, so a full heap costs sounds rather than the game.
			const std::int32_t newCapacity = _bufferCapacity * 2;
			const std::int32_t newBytes = newCapacity * std::int32_t(sizeof(Buffer));
			Buffer* grown = static_cast<Buffer*>(std::realloc(_buffers, std::size_t(newBytes)));
			if (grown == nullptr) {
				LOGE("Cannot grow the audio buffer table to {} entries ({} bytes, {} bytes of heap free)",
					newCapacity, newBytes, GetFreeHeapBytes());
				return 0;
			}
			_buffers = grown;
			_bufferCapacity = newCapacity;
		}

		::new (&_buffers[_bufferCount]) Buffer{};
		_buffers[_bufferCount].Used = true;
		return std::uint32_t(_bufferCount++);
	}

	void N64AudioDevice::ReleaseBuffer(Buffer& buffer)
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

		ReportResidentBytes();
	}

	void N64AudioDevice::ReportResidentBytes()
	{
		const std::int32_t moved = _residentBytes - _residentLogged;
		if (moved < ResidentLogStep && moved > -ResidentLogStep) {
			return;
		}
		_residentLogged = _residentBytes - (_residentBytes % ResidentLogStep);

		LOGI("Audio samples now hold {} bytes in {} buffers, {} bytes of heap free",
			_residentBytes, CountLoadedBuffers(), GetFreeHeapBytes());
	}

	std::int32_t N64AudioDevice::CountLoadedBuffers() const
	{
		std::int32_t count = 0;
		for (std::int32_t i = 1; i < _bufferCount; i++) {
			if (_buffers[i].Used && _buffers[i].Samples != nullptr) {
				count++;
			}
		}
		return count;
	}

	void N64AudioDevice::deleteBuffer(std::uint32_t bufferId)
	{
		if (bufferId == 0 || bufferId >= std::uint32_t(_bufferCount)) {
			return;
		}
		ReleaseBuffer(_buffers[bufferId]);
		_buffers[bufferId].Used = false;
	}

	bool N64AudioDevice::uploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency)
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

		// `size` is a byte count on every path into here, so a trailing partial frame is dropped
		// rather than turned into a sample count that disagrees with the data
		const std::int32_t frameSize = bytesPerSample * channelCount;
		const std::int32_t sourceFrames = size / frameSize;
		const std::int32_t sourceFrequency = (frequency > 0 ? frequency : _outputFrequency);

		// A sound past the cap is stored at half its rate, twice if it is still past it, and so on
		// until it fits or the floor stops it. The mixer resamples every source to the AI's rate
		// anyway, so a decimated sound still plays at its proper pitch and for its proper length and
		// only loses its high end - the same trade the Dreamcast backend makes when a sound outgrows
		// what an AICA channel can address. Halving is what keeps it cheap: the factor stays an
		// integer, so each output frame is the average of a whole group of input frames.
		std::int32_t factor = 1;
		while ((sourceFrames / factor) * frameSize > MaxBytesPerBuffer &&
				sourceFrequency / (factor * 2) >= MinDecimatedFrequency) {
			factor *= 2;
		}

		const std::int32_t frameCount = sourceFrames / factor;
		const std::int32_t byteCount = frameCount * frameSize;

		Buffer& buffer = _buffers[bufferId];
		if (byteCount <= 0) {
			ReleaseBuffer(buffer);
			return false;
		}

		// A streaming source uploads into the same buffer several times a second, so an allocation
		// that is already big enough is kept: freeing and reallocating at that rate would fragment a
		// heap this small in minutes. Only a request that outgrows it allocates.
		if (byteCount > buffer.Capacity) {
			// What the buffer already holds goes back to the heap before more is asked for: holding
			// both would need twice the peak this console does not have, and a growing streaming
			// buffer would otherwise be judged against a heap that still counted its own last chunk
			ReleaseBuffer(buffer);

			// Sample data is refused before the heap is exhausted rather than after, so the failure
			// lands here - with the numbers to tell an oversized sound from a genuinely full heap -
			// instead of on whatever allocates next
			const std::int32_t freeBytes = GetFreeHeapBytes();
			if (freeBytes - byteCount < MinFreeHeapBytes) {
				LOGE("Cannot allocate {} bytes for audio buffer {} - only {} bytes of heap free and {} "
					"have to remain ({} bytes hold samples already), the sound will be silent",
					byteCount, bufferId, freeBytes, MinFreeHeapBytes, _residentBytes);
				return false;
			}

			buffer.Samples = static_cast<std::uint8_t*>(std::malloc(std::size_t(byteCount)));
			if (buffer.Samples == nullptr) {
				LOGE("Cannot allocate {} bytes for audio buffer {} - the heap reported {} bytes free "
					"({} bytes hold samples already), the sound will be silent",
					byteCount, bufferId, freeBytes, _residentBytes);
				return false;
			}
			buffer.Capacity = byteCount;
			_residentBytes += byteCount;
			ReportResidentBytes();
		}

		// Samples keep the width they arrived in. The mixer works in 16 bits and widening here would
		// be one less shift per sample in it, but it would also double what this console's content
		// costs to hold - nearly all of the game's sounds are 8-bit - and RDRAM is the binding
		// constraint, not the CPU. 16-bit data arrives native-endian already (the asset readers swap
		// on load) and the AI plays native-endian, so no swap happens on either path. The 8-bit form
		// is unsigned with 128 at silence, which is the one conversion left to make.
		if (factor == 1) {
			if (bytesPerSample == 1) {
				const std::uint8_t* source = static_cast<const std::uint8_t*>(data);
				std::int8_t* dest = reinterpret_cast<std::int8_t*>(buffer.Samples);
				for (std::int32_t i = 0; i < byteCount; i++) {
					dest[i] = std::int8_t(std::int32_t(source[i]) - 128);
				}
			} else {
				std::memcpy(buffer.Samples, data, std::size_t(byteCount));
			}
		} else {
			// Each output frame is the average of the group it replaces, not the first of it. Keeping
			// one sample in four and discarding the rest would fold everything above the new Nyquist
			// back into the audible band as a whistle over the sound; a box average over the group is
			// the cheapest thing that does not, and it runs here, once, not per frame in the mixer.
			//
			// Reading cannot run past the source: frameCount is sourceFrames/factor rounded down, so
			// the last group ends at frameCount*factor - 1 at the latest.
			if (bytesPerSample == 1) {
				const std::uint8_t* source = static_cast<const std::uint8_t*>(data);
				std::int8_t* dest = reinterpret_cast<std::int8_t*>(buffer.Samples);
				for (std::int32_t f = 0; f < frameCount; f++) {
					for (std::int32_t c = 0; c < channelCount; c++) {
						std::int32_t accumulator = 0;
						for (std::int32_t k = 0; k < factor; k++) {
							accumulator += std::int32_t(source[(f * factor + k) * channelCount + c]) - 128;
						}
						dest[f * channelCount + c] = std::int8_t(accumulator / factor);
					}
				}
			} else {
				const std::int16_t* source = static_cast<const std::int16_t*>(data);
				std::int16_t* dest = reinterpret_cast<std::int16_t*>(buffer.Samples);
				for (std::int32_t f = 0; f < frameCount; f++) {
					for (std::int32_t c = 0; c < channelCount; c++) {
						std::int32_t accumulator = 0;
						for (std::int32_t k = 0; k < factor; k++) {
							accumulator += source[(f * factor + k) * channelCount + c];
						}
						dest[f * channelCount + c] = std::int16_t(accumulator / factor);
					}
				}
			}

			LOGI("Audio buffer {} holds {} bytes at {} Hz instead of {} at {} Hz - past the {} byte cap, "
				"stored at a {}x lower rate ({} bytes hold samples now)",
				bufferId, byteCount, sourceFrequency / factor, sourceFrames * frameSize, sourceFrequency,
				MaxBytesPerBuffer, factor, _residentBytes);
		}

		buffer.BytesPerSample = bytesPerSample;
		buffer.ChannelCount = channelCount;
		// The stored rate is what the mixer steps the cursor by, so the mixer never has to know the
		// sound was reduced; only the sample-offset accessors do (see Buffer::Decimation)
		buffer.Frequency = sourceFrequency / factor;
		buffer.FrameCount = frameCount;
		buffer.Decimation = factor;
		return true;
	}

	void N64AudioDevice::setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->BufferId = bufferId;
			source->Cursor = 0;
		}
	}

	void N64AudioDevice::setSourceGain(std::uint32_t sourceId, float gain)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Gain = gain;
		}
	}

	void N64AudioDevice::setSourcePitch(std::uint32_t sourceId, float pitch)
	{
		if (Source* source = GetSource(sourceId)) {
			// A non-positive pitch would stall or reverse the cursor, neither of which the mixer expresses
			source->Pitch = (pitch > 0.0f ? pitch : 1.0f);
		}
	}

	void N64AudioDevice::setSourceLooping(std::uint32_t sourceId, bool looping)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Looping = looping;
		}
	}

	void N64AudioDevice::setSourceRelative(std::uint32_t sourceId, bool relative)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Relative = relative;
		}
	}

	void N64AudioDevice::setSourcePosition(std::uint32_t sourceId, const Vector3f& position)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Position = position;
		}
	}

	void N64AudioDevice::setSourceLowPass(std::uint32_t sourceId, float value)
	{
		// The AI has no per-voice DSP - it is a DMA engine in front of a DAC - and filtering in the
		// mixer would cost every source a per-sample branch for the single effect that asks for it
		static_cast<void>(sourceId);
		static_cast<void>(value);
	}

	std::int32_t N64AudioDevice::sourceSampleOffset(std::uint32_t sourceId)
	{
		Source* source = GetSource(sourceId);
		if (source == nullptr) {
			return 0;
		}
		// Callers speak in the ORIGINAL sound's sample space; the cursor counts stored (possibly
		// decimated) frames, so the answer is scaled back up by what one stored frame stands for
		const Buffer* buffer = GetActiveBuffer(*source);
		const std::int32_t decimation = (buffer != nullptr ? buffer->Decimation : 1);
		return std::int32_t(source->Cursor >> 32) * decimation;
	}

	void N64AudioDevice::setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset)
	{
		if (Source* source = GetSource(sourceId)) {
			// The inverse of sourceSampleOffset(): an original-space offset lands on the stored frame
			// that stands for it
			const Buffer* buffer = GetActiveBuffer(*source);
			const std::int32_t decimation = (buffer != nullptr ? buffer->Decimation : 1);
			source->Cursor = std::int64_t(offset > 0 ? offset / decimation : 0) << 32;
		}
	}

	void N64AudioDevice::playSource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Playing = true;
			source->Paused = false;
		}
	}

	void N64AudioDevice::pauseSource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Paused = true;
		}
	}

	void N64AudioDevice::stopSource(std::uint32_t sourceId)
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

	bool N64AudioDevice::isSourcePlaying(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr && source->Playing && !source->Paused);
	}

	void N64AudioDevice::queueBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
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

	std::int32_t N64AudioDevice::numProcessedBuffers(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr ? source->ProcessedCount : 0);
	}

	void N64AudioDevice::unqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds)
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

	void N64AudioDevice::suspendDevice()
	{
		// Nothing to program: once the queue stops being topped up the AI drains what it holds -
		// at most the ~160 ms of headroom - and falls silent on its own
		_suspended = true;
	}

	void N64AudioDevice::resumeDevice()
	{
		// The drained queue holds nothing stale, so resuming is just topping it up again
		_suspended = false;
	}

	void N64AudioDevice::updatePlayers()
	{
		// The base class advances the players and retires the finished ones first, so the mix below
		// sees the state this frame actually asked for
		AudioDeviceBase::updatePlayers();

		if (_valid && !_suspended) {
			FillQueue();
		}
	}

	void N64AudioDevice::FillQueue()
	{
		// audio_write_begin() blocks when every buffer is queued, which would stall the frame on the
		// DAC, so each iteration is gated on audio_can_write() first.
		//
		// The count is capped rather than "however many the queue will take". Those are the same thing
		// while the game keeps up: at 30 fps a frame consumes under two of the queue's 20 ms buffers.
		// They stop being the same thing when a frame runs long - then the queue has drained and
		// refilling all of it means mixing 160 ms of audio in one frame, which makes the next frame
		// later still. Capping the catch-up bounds what a slow frame costs; the audio breaks up while
		// the game is that far behind either way, and the queue refills over the following frames.
		for (std::int32_t i = 0; i < MaxBuffersPerFill && audio_can_write(); i++) {
			std::int16_t* output = audio_write_begin();
			if (output == nullptr) {
				break;
			}
			MixInto(output, _bufferFrames);
			audio_write_end();
		}
	}

	N64AudioDevice::Buffer* N64AudioDevice::GetActiveBuffer(Source& source)
	{
		// A streaming source reads the head of its queue, a static one its single attached buffer
		const std::uint32_t bufferId = (source.QueueCount > 0 ? source.Queue[0] : source.BufferId);
		if (bufferId == 0 || bufferId >= std::uint32_t(_bufferCount)) {
			return nullptr;
		}
		Buffer& buffer = _buffers[bufferId];
		return (buffer.Used && buffer.Samples != nullptr && buffer.FrameCount > 0 ? &buffer : nullptr);
	}

	// Always inlined: this is called twice per OUTPUT SAMPLE by the resampler below, tens of thousands
	// of times a frame, and out of line it cost a call, two stores through the reference parameters and
	// a re-test of the format on every one of them - the compiler hoists all three once it is inlined,
	// because the format and channel count are loop invariants of MixSource
	DEATH_ALWAYS_INLINE void N64AudioDevice::ReadFrame(const Buffer& buffer, std::int32_t frame, std::int32_t& left, std::int32_t& right)
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

	void N64AudioDevice::ComputePanning(const Source& source, float& leftGain, float& rightGain) const
	{
		// The positional model is shared with the other software-mixing backends, so it sounds the same
		AudioMixer::ComputeStereoGains(source.Relative, source.Position, _listenerPos, source.Gain, _gain, leftGain, rightGain);
	}

	bool N64AudioDevice::MixSource(Source& source, std::int32_t* output, std::int32_t frames)
	{
		Buffer* buffer = GetActiveBuffer(source);
		if (buffer == nullptr) {
			return false;
		}

		// The gains are computed in float - the same math as every other backend, so panning sounds
		// identical - then held in Q15 for the block, keeping the per-sample loop entirely in integer
		// registers. Both are at most 1.0, so a Q15 gain times a 16-bit sample stays inside 31 bits.
		float leftGain, rightGain;
		ComputePanning(source, leftGain, rightGain);
		const std::int32_t leftQ15 = std::int32_t(leftGain * 32768.0f + 0.5f);
		const std::int32_t rightQ15 = std::int32_t(rightGain * 32768.0f + 0.5f);

		// One output frame advances the cursor by this much of an input frame (in the cursor's 32.32
		// fixed point), which is where both the source's pitch and the rate difference between the
		// buffer and the AI are applied. Computed in floating point once per block and per buffer -
		// the per-sample loop below never touches the FPU.
		std::int64_t step = std::int64_t((double(buffer->Frequency) / double(_outputFrequency)) * double(source.Pitch) * 4294967296.0);
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
					// The next buffer may be stored at another rate (or decimation), so the step follows
					// it - and the loop re-tests rather than skipping this output frame, so a buffer
					// boundary no longer costs a one-sample gap
					step = std::int64_t((double(buffer->Frequency) / double(_outputFrequency)) * double(source.Pitch) * 4294967296.0);
					end = std::int64_t(buffer->FrameCount) << 32;
				} else if (source.Looping) {
					// Wrapped rather than reset, so a step that overshoots the end does not lose the
					// fraction of a frame it went past by - over a long loop that would drift audibly
					// (GetActiveBuffer() guarantees FrameCount > 0, so `end` is never zero here)
					source.Cursor %= end;
				} else {
					return false;
				}
			}

			// Linear interpolation between the two frames the cursor sits between, with the fraction
			// held in Q8 - plenty for 16-bit samples. The upper neighbour is clamped to the last frame
			// rather than wrapped: at the very end of a non-looping buffer there is nothing after it,
			// and wrapping would fold the first sample into the last one as a click.
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

	void N64AudioDevice::MixInto(std::int16_t* output, std::int32_t frames)
	{
		// Silence is the common case - a menu with the music off, and most frames of a level between
		// effects - and it does not need the accumulator at all. Zeroing 32 bits per channel and then
		// converting every one of them back down to 16 is twice the memory traffic of writing the silence
		// straight out, and the queue is topped up with a buffer or two of it every single frame.
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
				// Ran out of audio: a static source has finished, and a streaming one has been starved
				// by a decoder that could not keep up. Both stop; the player notices through
				// isSourcePlaying().
				source.Playing = false;
				source.Cursor = 0;
			}
		}

		// The DAC takes 16 bits and wrapping the excess would flip its polarity, a far worse artefact
		// than the clipping this is instead. With this many voices a loud moment can exceed the range,
		// which is the whole reason the accumulator is wider than the output.
		//
		// The output is libdragon's AI buffer, which it allocates UNCACHED - every store goes straight
		// to RDRAM at full memory latency, with no write gathering. Both channels of a frame are
		// therefore packed into one 32-bit word first, halving those transactions (the AI plays the
		// left sample from the lower address, which is where the high half of a big-endian word lands).
		const std::int32_t total = frames * ChannelCount;
		std::uint32_t* output32 = reinterpret_cast<std::uint32_t*>(output);
		for (std::int32_t i = 0; i < total; i += 2) {
			std::int32_t left = accumulator[i];
			std::int32_t right = accumulator[i + 1];
			left = (left < -32768 ? -32768 : (left > 32767 ? 32767 : left));
			right = (right < -32768 ? -32768 : (right > 32767 ? 32767 : right));
			output32[i >> 1] = (std::uint32_t(std::uint16_t(left)) << 16) | std::uint32_t(std::uint16_t(right));
		}
	}
}

#endif
