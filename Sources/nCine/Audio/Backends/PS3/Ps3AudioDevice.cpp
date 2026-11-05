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
		// The audio port lives in a firmware module that must be loaded first
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

		// Firmware-owned memory: the ring the hardware scans out, and the block index it last read (polled, no event queue)
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
		SetSourcePool(arrayView(sourceIds, MaxSources));

		_valid = true;
		LOGI("Audio device initialized: PSL1GHT libaudio, {} Hz stereo, {} blocks of {} samples",
			OutputFrequency, BlockCount, BlockSamples);
	}

	Ps3AudioDevice::~Ps3AudioDevice()
	{
		// The base class's decoding thread can still be handing buffers over, so it goes first
		ShutdownDecodeThread();

		if (_valid) {
			audioPortStop(_portNumber);
			audioPortClose(_portNumber);
			audioQuit();
			_valid = false;
		}
	}

	bool Ps3AudioDevice::IsValid() const
	{
		return _valid;
	}

	const char* Ps3AudioDevice::GetName() const
	{
		return "libaudio";
	}

	void Ps3AudioDevice::SetGain(float gain)
	{
		// Applied while mixing; changing the port level would also affect audio already in the ring
		_gain = gain;
	}

	void Ps3AudioDevice::UpdateListener(const Vector3f& position, const Vector3f& velocity)
	{
		// No Doppler on this backend, so the velocity is not kept
		static_cast<void>(velocity);
		_listenerPos = position;
	}

	std::int32_t Ps3AudioDevice::GetNativeFrequency()
	{
		return OutputFrequency;
	}

	std::uint32_t Ps3AudioDevice::RegisterPlayer(IAudioPlayer* player)
	{
		const std::uint32_t sourceId = AudioDeviceBase::RegisterPlayer(player);
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

	std::uint32_t Ps3AudioDevice::CreateBuffer(BufferUsage usage)
	{
		// Everything lives in main memory, so the usage is irrelevant
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

	void Ps3AudioDevice::DeleteBuffer(std::uint32_t bufferId)
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

	bool Ps3AudioDevice::UploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency)
	{
		if (bufferId == 0 || bufferId >= _buffers.size() || data == nullptr || size <= 0) {
			return false;
		}

		Buffer& buffer = _buffers[bufferId];
		buffer.Frequency = (frequency > 0 ? frequency : OutputFrequency);

		// Normalized to interleaved 16-bit; 8-bit input is unsigned with 128 at silence
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

	void Ps3AudioDevice::SetSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->BufferId = bufferId;
			source->Cursor = 0.0;
		}
	}

	void Ps3AudioDevice::SetSourceGain(std::uint32_t sourceId, float gain)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Gain = gain;
		}
	}

	void Ps3AudioDevice::SetSourcePitch(std::uint32_t sourceId, float pitch)
	{
		if (Source* source = GetSource(sourceId)) {
			// A non-positive pitch would stall or reverse the cursor, neither of which the mixer expresses
			source->Pitch = (pitch > 0.0f ? pitch : 1.0f);
		}
	}

	void Ps3AudioDevice::SetSourceLooping(std::uint32_t sourceId, bool looping)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Looping = looping;
		}
	}

	void Ps3AudioDevice::SetSourceRelative(std::uint32_t sourceId, bool relative)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Relative = relative;
		}
	}

	void Ps3AudioDevice::SetSourcePosition(std::uint32_t sourceId, const Vector3f& position)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Position = position;
		}
	}

	void Ps3AudioDevice::SetSourceLowPass(std::uint32_t sourceId, float value)
	{
		// No filter stage, it would cost every source a per-sample branch
		static_cast<void>(sourceId);
		static_cast<void>(value);
	}

	std::int32_t Ps3AudioDevice::GetSourceSampleOffset(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr ? std::int32_t(source->Cursor) : 0);
	}

	void Ps3AudioDevice::SetSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Cursor = double(offset > 0 ? offset : 0);
		}
	}

	void Ps3AudioDevice::PlaySource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Playing = true;
			source->Paused = false;
		}
	}

	void Ps3AudioDevice::PauseSource(std::uint32_t sourceId)
	{
		if (Source* source = GetSource(sourceId)) {
			source->Paused = true;
		}
	}

	void Ps3AudioDevice::StopSource(std::uint32_t sourceId)
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

	bool Ps3AudioDevice::IsSourcePlaying(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr && source->Playing && !source->Paused);
	}

	void Ps3AudioDevice::QueueBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
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

	std::int32_t Ps3AudioDevice::GetProcessedBufferCount(std::uint32_t sourceId)
	{
		const Source* source = GetSource(sourceId);
		return (source != nullptr ? source->ProcessedCount : 0);
	}

	void Ps3AudioDevice::UnqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds)
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

	void Ps3AudioDevice::SuspendDevice()
	{
		if (_valid && !_suspended) {
			audioPortStop(_portNumber);
			_suspended = true;
		}
	}

	void Ps3AudioDevice::ResumeDevice()
	{
		if (_valid && _suspended) {
			// Clear stale audio from the ring and resync the write cursor with the hardware
			std::memset(_portBuffer, 0, std::size_t(BlockCount) * BlockSamples * ChannelCount * sizeof(float));
			audioPortStart(_portNumber);
			_writeBlock = std::uint32_t(*_readIndexAddress);
			_suspended = false;
		}
	}

	void Ps3AudioDevice::OnBlockingOperationBegan()
	{
		// The hardware would keep looping the ring while UpdatePlayers() is not called, so silence it;
		// the next FillRing() puts the write cursor back just ahead of the hardware
		if (_valid && !_suspended) {
			std::memset(_portBuffer, 0, std::size_t(BlockCount) * BlockSamples * ChannelCount * sizeof(float));
			_writeBlock = BlockCount;
		}
	}

	void Ps3AudioDevice::OnBlockingOperationEnded()
	{
		// Nothing to do: the next FillRing() resynchronizes the write cursor (see OnBlockingOperationBegan())
	}

	void Ps3AudioDevice::UpdatePlayers()
	{
		// Base class first, so the mix below reflects this frame's state
		AudioDeviceBase::UpdatePlayers();

		if (_valid && !_suspended) {
			FillRing();
		}
	}

	void Ps3AudioDevice::FillRing()
	{
		// Every block except the one the hardware is reading is ours to write
		const std::uint32_t readBlock = std::uint32_t(*_readIndexAddress) % BlockCount;

		// After the first call or a blocking operation, start just ahead of the hardware
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

		// Cursor advance per output frame, combining pitch and the buffer-to-port rate ratio
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
					// The next buffer may have another rate; the loop re-tests instead of skipping a frame
					step = (double(buffer->Frequency) / double(OutputFrequency)) * double(source.Pitch);
				} else if (source.Looping) {
					// Wrapped rather than reset, so the overshoot is kept and long loops do not drift
					source.Cursor = std::fmod(source.Cursor, double(buffer->FrameCount));
				} else {
					return false;
				}
			}

			// Linear interpolation; the upper neighbour is clamped, not wrapped, to avoid a click at the end
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
				// Finished or starved; the player notices through IsSourcePlaying()
				source.Playing = false;
				source.Cursor = 0.0;
			}
		}

		// The hardware wraps values outside -1..1 into the opposite polarity, so the sum is clamped
		const std::uint32_t total = frames * ChannelCount;
		for (std::uint32_t i = 0; i < total; i++) {
			const float value = output[i];
			output[i] = (value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value));
		}
	}
}

#endif
