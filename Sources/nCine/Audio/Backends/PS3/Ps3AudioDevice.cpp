#if defined(WITH_PS3AUDIO)

#include "Ps3AudioDevice.h"
#include "../../AudioMixerCommon.h"
#include "../../IAudioPlayer.h"
#include "../../../../Main.h"

#include <cmath>
#include <cstring>

#include <audio/audio.h>
#include <sysmodule/sysmodule.h>

namespace nCine
{
	Ps3AudioDevice::Ps3AudioDevice()
		: _valid(false), _suspended(false), _portNumber(0), _portBuffer(nullptr),
			_readIndexAddress(nullptr), _writeBlock(0)
	{
		// The audio port lives in a firmware module rather than in the ELF, so it has to be pulled in
		// before any of the calls below resolve
		if (sysModuleLoad(SYSMODULE_AUDIO) != 0) {
			LOGE("Cannot load the audio system module, sound will be disabled");
			return;
		}
		if (audioInit() != 0) {
			LOGE("Cannot initialize the audio subsystem, sound will be disabled");
			return;
		}

		audioPortParam params;
		std::memset(&params, 0, sizeof(params));
		params.numChannels = ChannelCount;
		params.numBlocks = BlockCount;
		params.attrib = 0;
		params.level = 1.0f;

		if (audioPortOpen(&params, &_portNumber) != 0) {
			LOGE("Cannot open an audio port, sound will be disabled");
			audioQuit();
			return;
		}

		audioPortConfig config;
		std::memset(&config, 0, sizeof(config));
		if (audioGetPortConfig(_portNumber, &config) != 0) {
			LOGE("Cannot query the audio port configuration, sound will be disabled");
			audioPortClose(_portNumber);
			audioQuit();
			return;
		}

		// Both of these are lv2 addresses of memory the firmware owns: the ring the hardware scans out, and
		// the block index it has most recently read. Nothing here waits on an event queue - the ring is
		// topped up from updatePlayers() instead (see the class documentation) - so the read index is the
		// only thing that says how much of it has been consumed.
		_portBuffer = reinterpret_cast<float*>(std::uint64_t(config.audioDataStart));
		_readIndexAddress = reinterpret_cast<std::uint64_t*>(std::uint64_t(config.readIndex));

		if (audioPortStart(_portNumber) != 0) {
			LOGE("Cannot start the audio port, sound will be disabled");
			audioPortClose(_portNumber);
			audioQuit();
			return;
		}

		std::memset(_portBuffer, 0, std::size_t(BlockCount) * BlockSamples * ChannelCount * sizeof(float));
		_mixBuffer.resize_for_overwrite(std::size_t(BlockSamples) * ChannelCount);

		// Buffer id 0 is reserved as "no buffer", so the table starts with an unused entry
		_buffers.emplace_back();

		std::uint32_t sourceIds[MaxSources];
		for (std::int32_t i = 0; i < MaxSources; i++) {
			sourceIds[i] = std::uint32_t(i + 1);
		}
		setSourcePool(arrayView(sourceIds, MaxSources));

		_valid = true;
		LOGI("Audio device initialized: PSL1GHT libaudio, {} Hz stereo, {} blocks of {} samples",
			OutputFrequency, BlockCount, BlockSamples);
	}

	Ps3AudioDevice::~Ps3AudioDevice()
	{
		// The base class's decoding thread can still be handing buffers over, so it goes first
		shutdownDecodeThread();

		if (_valid) {
			audioPortStop(_portNumber);
			audioPortClose(_portNumber);
			audioQuit();
			_valid = false;
		}
	}

	bool Ps3AudioDevice::isValid() const
	{
		return _valid;
	}

	const char* Ps3AudioDevice::name() const
	{
		return "libaudio";
	}

	void Ps3AudioDevice::setGain(float gain)
	{
		// Applied while mixing rather than programmed anywhere: the port's own level is set once at open
		// time, and changing it would fade the whole ring including audio already written into it
		_gain = gain;
	}

	void Ps3AudioDevice::updateListener(const Vector3f& position, const Vector3f& velocity)
	{
		// No Doppler on this backend, so the velocity is not kept
		static_cast<void>(velocity);
		_listenerPos = position;
	}

	std::int32_t Ps3AudioDevice::nativeFrequency()
	{
		return OutputFrequency;
	}

	std::uint32_t Ps3AudioDevice::registerPlayer(IAudioPlayer* player)
	{
		const std::uint32_t sourceId = AudioDeviceBase::registerPlayer(player);
		if (sourceId != UnavailableSource) {
			if (Source* source = GetSource(sourceId)) {
				*source = Source{};
			}
		}
		return sourceId;
	}

	Ps3AudioDevice::Source* Ps3AudioDevice::GetSource(std::uint32_t sourceId)
	{
		if (sourceId == 0 || sourceId > std::uint32_t(MaxSources)) {
			return nullptr;
		}
		return &_sources[sourceId - 1];
	}

	std::uint32_t Ps3AudioDevice::createBuffer(BufferUsage usage)
	{
		// Both kinds live in main memory here - there is no separate sound RAM to place them in - so the
		// usage says nothing this backend can act on
		static_cast<void>(usage);

		for (std::uint32_t i = 1; i < _buffers.size(); i++) {
			if (!_buffers[i].Used) {
				_buffers[i] = Buffer{};
				_buffers[i].Used = true;
				return i;
			}
		}
		_buffers.emplace_back();
		_buffers.back().Used = true;
		return std::uint32_t(_buffers.size() - 1);
	}

	void Ps3AudioDevice::deleteBuffer(std::uint32_t bufferId)
	{
		if (bufferId == 0 || bufferId >= _buffers.size()) {
			return;
		}
		// A source still reading this buffer would walk freed samples, so it is stopped first
		for (Source& source : _sources) {
			if (source.BufferId == bufferId) {
				source.Playing = false;
				source.BufferId = 0;
			}
		}
		_buffers[bufferId] = Buffer{};
	}

	bool Ps3AudioDevice::uploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency)
	{
		if (bufferId == 0 || bufferId >= _buffers.size() || data == nullptr || size <= 0) {
			return false;
		}

		Buffer& buffer = _buffers[bufferId];
		buffer.Frequency = (frequency > 0 ? frequency : OutputFrequency);

		// Everything is normalized to interleaved 16-bit here, so the mixer has one input format to read.
		// The 8-bit forms are unsigned with 128 at silence, which is the one conversion worth spelling out.
		switch (format) {
			case BufferFormat::Mono8:
			case BufferFormat::Stereo8: {
				buffer.ChannelCount = (format == BufferFormat::Stereo8 ? 2 : 1);
				const std::uint8_t* source = static_cast<const std::uint8_t*>(data);
				buffer.Samples.resize_for_overwrite(std::size_t(size));
				for (std::int32_t i = 0; i < size; i++) {
					buffer.Samples[i] = std::int16_t((std::int32_t(source[i]) - 128) << 8);
				}
				buffer.FrameCount = size / buffer.ChannelCount;
				break;
			}
			case BufferFormat::Mono16:
			case BufferFormat::Stereo16: {
				buffer.ChannelCount = (format == BufferFormat::Stereo16 ? 2 : 1);
				const std::int32_t sampleCount = size / std::int32_t(sizeof(std::int16_t));
				buffer.Samples.resize_for_overwrite(std::size_t(sampleCount));
				std::memcpy(buffer.Samples.data(), data, std::size_t(sampleCount) * sizeof(std::int16_t));
				buffer.FrameCount = sampleCount / buffer.ChannelCount;
				break;
			}
			default:
				return false;
		}
		return true;
	}

	void Ps3AudioDevice::setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->BufferId = bufferId;
			source->Cursor = 0.0;
		}
	}

	void Ps3AudioDevice::setSourceGain(std::uint32_t sourceId, float gain)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Gain = gain;
		}
	}

	void Ps3AudioDevice::setSourcePitch(std::uint32_t sourceId, float pitch)
	{
		if (Source* source = GetSource(sourceId)) {
			// A non-positive pitch would stall or reverse the cursor, neither of which the mixer expresses
			source->Pitch = (pitch > 0.0f ? pitch : 1.0f);
		}
	}

	void Ps3AudioDevice::setSourceLooping(std::uint32_t sourceId, bool looping)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Looping = looping;
		}
	}

	void Ps3AudioDevice::setSourceRelative(std::uint32_t sourceId, bool relative)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Relative = relative;
		}
	}

	void Ps3AudioDevice::setSourcePosition(std::uint32_t sourceId, const Vector3f& position)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Position = position;
		}
	}

	void Ps3AudioDevice::setSourceLowPass(std::uint32_t sourceId, float value)
	{
		// The mixer has no filter stage, and adding one for the single effect that asks for it would cost
		// every source a per-sample branch
		static_cast<void>(sourceId);
		static_cast<void>(value);
	}

	std::int32_t Ps3AudioDevice::sourceSampleOffset(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr ? std::int32_t(source->Cursor) : 0);
	}

	void Ps3AudioDevice::setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Cursor = double(offset > 0 ? offset : 0);
		}
	}

	void Ps3AudioDevice::playSource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Playing = true;
			source->Paused = false;
		}
	}

	void Ps3AudioDevice::pauseSource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Paused = true;
		}
	}

	void Ps3AudioDevice::stopSource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Playing = false;
			source->Paused = false;
			source->Cursor = 0.0;
			// A stopped streaming source hands its queue back, which is what the player expects to collect
			for (std::int32_t i = 0; i < source->QueueCount && source->ProcessedCount < MaxQueuedBuffers; i++) {
				source->Processed[source->ProcessedCount++] = source->Queue[i];
			}
			source->QueueCount = 0;
		}
	}

	bool Ps3AudioDevice::isSourcePlaying(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr && source->Playing && !source->Paused);
	}

	void Ps3AudioDevice::queueBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		Source* source = GetSource(sourceId);
		if (source == nullptr || bufferId == 0 || bufferId >= _buffers.size()) {
			return;
		}
		if (source->QueueCount >= MaxQueuedBuffers) {
			LOGW("Audio source {} queue is full, dropping a buffer", sourceId);
			return;
		}
		source->Queue[source->QueueCount++] = bufferId;
	}

	std::int32_t Ps3AudioDevice::numProcessedBuffers(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr ? source->ProcessedCount : 0);
	}

	void Ps3AudioDevice::unqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds)
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

	void Ps3AudioDevice::suspendDevice()
	{
		if (_valid && !_suspended) {
			audioPortStop(_portNumber);
			_suspended = true;
		}
	}

	void Ps3AudioDevice::resumeDevice()
	{
		if (_valid && _suspended) {
			// The ring still holds whatever was in it when the port stopped, which would be replayed as a
			// burst of stale audio; the write cursor is resynchronized with the hardware instead
			std::memset(_portBuffer, 0, std::size_t(BlockCount) * BlockSamples * ChannelCount * sizeof(float));
			audioPortStart(_portNumber);
			_writeBlock = std::uint32_t(*_readIndexAddress);
			_suspended = false;
		}
	}

	void Ps3AudioDevice::updatePlayers()
	{
		// The base class advances the players and retires the finished ones first, so the mix below sees the
		// state this frame actually asked for
		AudioDeviceBase::updatePlayers();

		if (_valid && !_suspended) {
			FillRing();
		}
	}

	void Ps3AudioDevice::FillRing()
	{
		// The hardware publishes the block it is reading; everything from the one after it up to (but not
		// including) it again is ours to write. Stopping one block short of the read cursor keeps the mixer
		// from overwriting the block currently being scanned out.
		const std::uint32_t readBlock = std::uint32_t(*_readIndexAddress) % BlockCount;

		// A first call (or a resume) starts writing just ahead of the hardware rather than wherever the
		// cursor happened to be left, so the ring fills forwards from a known point
		if (_writeBlock >= BlockCount) {
			_writeBlock = (readBlock + 1) % BlockCount;
		}

		const std::uint32_t blockFloats = BlockSamples * ChannelCount;
		for (std::uint32_t i = 0; i < BlockCount; i++) {
			if (_writeBlock == readBlock) {
				// Caught up with the hardware: the ring is as full as it can safely be
				break;
			}
			float* block = _portBuffer + std::size_t(_writeBlock) * blockFloats;
			MixInto(block, BlockSamples);
			_writeBlock = (_writeBlock + 1) % BlockCount;
		}
	}

	Ps3AudioDevice::Buffer* Ps3AudioDevice::GetActiveBuffer(Source& source)
	{
		// A streaming source reads the head of its queue, a static one its single attached buffer
		const std::uint32_t bufferId = (source.QueueCount > 0 ? source.Queue[0] : source.BufferId);
		if (bufferId == 0 || bufferId >= _buffers.size()) {
			return nullptr;
		}
		Buffer& buffer = _buffers[bufferId];
		return (buffer.Used && buffer.FrameCount > 0 ? &buffer : nullptr);
	}

	void Ps3AudioDevice::ComputePanning(const Source& source, float& leftGain, float& rightGain) const
	{
		// The positional model is shared with the other software-mixing backends, so it sounds the same
		AudioMixer::ComputeStereoGains(source.Relative, source.Position, _listenerPos, source.Gain, _gain, leftGain, rightGain);
	}

	bool Ps3AudioDevice::MixSource(Source& source, float* output, std::uint32_t frames)
	{
		Buffer* buffer = GetActiveBuffer(source);
		if (buffer == nullptr) {
			return false;
		}

		float leftGain, rightGain;
		ComputePanning(source, leftGain, rightGain);

		// One output frame advances the cursor by this much of an input frame, which is where both the
		// source's pitch and the rate difference between the buffer and the port are applied
		double step = (double(buffer->Frequency) / double(OutputFrequency)) * double(source.Pitch);

		for (std::uint32_t i = 0; i < frames; i++) {
			while (source.Cursor >= double(buffer->FrameCount)) {
				if (source.QueueCount > 0) {
					// A streaming source moves on to the next queued buffer, handing the exhausted one back
					if (source.ProcessedCount < MaxQueuedBuffers) {
						source.Processed[source.ProcessedCount++] = source.Queue[0];
					}
					for (std::int32_t q = 1; q < source.QueueCount; q++) {
						source.Queue[q - 1] = source.Queue[q];
					}
					source.QueueCount--;
					source.Cursor -= double(buffer->FrameCount);
					buffer = GetActiveBuffer(source);
					if (buffer == nullptr) {
						return false;
					}
					// The next buffer may carry another sample rate, so the step follows it - and the loop
					// re-tests rather than skipping this output frame, so a buffer boundary no longer costs
					// a one-sample gap (mirrors the N64 mixer)
					step = (double(buffer->Frequency) / double(OutputFrequency)) * double(source.Pitch);
				} else if (source.Looping) {
					// Wrapped rather than reset, so a step that overshoots the end does not lose the
					// fraction of a frame it went past by - over a long loop that would drift audibly
					source.Cursor = std::fmod(source.Cursor, double(buffer->FrameCount));
				} else {
					return false;
				}
			}

			// Linear interpolation between the two frames the cursor sits between. The upper neighbour is
			// clamped to the last frame rather than wrapped: at the very end of a non-looping buffer there
			// is nothing after it, and wrapping would fold the first sample into the last one as a click.
			const std::int32_t index = std::int32_t(source.Cursor);
			const float fraction = float(source.Cursor - double(index));
			const std::int32_t nextIndex = (index + 1 < buffer->FrameCount ? index + 1 : index);
			const std::int32_t channels = buffer->ChannelCount;
			const std::int16_t* samples = buffer->Samples.data();

			float left, right;
			if (channels == 2) {
				const float l0 = float(samples[index * 2]);
				const float l1 = float(samples[nextIndex * 2]);
				const float r0 = float(samples[index * 2 + 1]);
				const float r1 = float(samples[nextIndex * 2 + 1]);
				left = (l0 + (l1 - l0) * fraction) / 32768.0f;
				right = (r0 + (r1 - r0) * fraction) / 32768.0f;
			} else {
				const float s0 = float(samples[index]);
				const float s1 = float(samples[nextIndex]);
				left = right = (s0 + (s1 - s0) * fraction) / 32768.0f;
			}

			output[i * ChannelCount] += left * leftGain;
			output[i * ChannelCount + 1] += right * rightGain;
			source.Cursor += step;
		}
		return true;
	}

	void Ps3AudioDevice::MixInto(float* output, std::uint32_t frames)
	{
		std::memset(output, 0, std::size_t(frames) * ChannelCount * sizeof(float));

		for (Source& source : _sources) {
			if (!source.Playing || source.Paused) {
				continue;
			}
			if (!MixSource(source, output, frames)) {
				// Ran out of audio: a static source has finished, and a streaming one has been starved by a
				// decoder that could not keep up. Both stop; the player notices through isSourcePlaying().
				source.Playing = false;
				source.Cursor = 0.0;
			}
		}

		// The hardware takes -1..1 and wraps anything outside it into the opposite polarity, which is a far
		// worse artefact than the clipping it would otherwise be. With this many voices a loud moment can
		// exceed unity, so the sum is clamped rather than left to fold over.
		const std::uint32_t total = frames * ChannelCount;
		for (std::uint32_t i = 0; i < total; i++) {
			const float value = output[i];
			output[i] = (value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value));
		}
	}
}

#endif
