#pragma once

#if defined(WITH_SDLAUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

namespace nCine
{
	/**
		@brief Implementation of @ref IAudioDevice on top of SDL2's audio queue

		For platforms with SDL2 but no OpenAL (AmigaOS 4, MorphOS). Sources are mixed in software on the main
		thread and handed to `SDL_QueueAudio()` rather than an audio callback, so no lock is needed; latency
		is bounded by @ref TargetQueuedFrames.
	*/
	class SdlAudioDevice : public AudioDeviceBase
	{
	public:
		SdlAudioDevice();
		~SdlAudioDevice() override;

		bool IsValid() const override;

		const char* GetName() const override;

		void SetGain(float gain) override;

		void UpdateListener(const Vector3f& position, const Vector3f& velocity) override;

		std::int32_t GetNativeFrequency() override;

		std::uint32_t RegisterPlayer(IAudioPlayer* player) override;
		void UpdatePlayers() override;

		std::uint32_t CreateBuffer(BufferUsage usage) override;
		void DeleteBuffer(std::uint32_t bufferId) override;
		bool UploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency) override;

		void SetSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId) override;
		void SetSourceGain(std::uint32_t sourceId, float gain) override;
		void SetSourcePitch(std::uint32_t sourceId, float pitch) override;
		void SetSourceLooping(std::uint32_t sourceId, bool looping) override;
		void SetSourceRelative(std::uint32_t sourceId, bool relative) override;
		void SetSourcePosition(std::uint32_t sourceId, const Vector3f& position) override;
		void SetSourceLowPass(std::uint32_t sourceId, float value) override;
		std::int32_t GetSourceSampleOffset(std::uint32_t sourceId) override;
		void SetSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset) override;
		void PlaySource(std::uint32_t sourceId) override;
		void PauseSource(std::uint32_t sourceId) override;
		void StopSource(std::uint32_t sourceId) override;
		bool IsSourcePlaying(std::uint32_t sourceId) override;

		void QueueBuffer(std::uint32_t sourceId, std::uint32_t bufferId) override;
		std::int32_t GetProcessedBufferCount(std::uint32_t sourceId) override;
		void UnqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds) override;

		void SuspendDevice() override;
		void ResumeDevice() override;

	private:
		/** @brief Sources the mixer walks; a silent one costs nothing */
		static constexpr std::int32_t MaxSources = 32;
		/** @brief Upper bound on the streaming queue of a source (@ref AudioStream uses three) */
		static constexpr std::int32_t MaxQueuedBuffers = 4;
		/** @brief The device is opened as stereo; SDL downmixes if the hardware is not */
		static constexpr std::int32_t ChannelCount = 2;
		/** @brief Frames mixed per block, and the size SDL is asked to call for */
		static constexpr std::int32_t BlockFrames = 1024;
		/**
			@brief Frames the queue is kept topped up to

			Both the latency and the headroom for a late frame, since the queue is refilled once per
			rendered frame (about 93 ms at 44.1 kHz).
		*/
		static constexpr std::int32_t TargetQueuedFrames = 4 * BlockFrames;
		/** @brief Entries the buffer table starts with */
		static constexpr std::int32_t InitialBufferCapacity = 64;

		/** @brief One uploaded PCM buffer, interleaved (see the N64 backend for the rationale) */
		struct Buffer
		{
			bool Used = false;
			std::uint8_t* Samples = nullptr;
			std::int32_t Capacity = 0;
			std::int32_t BytesPerSample = 2;
			std::int32_t ChannelCount = 1;
			std::int32_t Frequency = 22050;
			std::int32_t FrameCount = 0;
		};

		/** @brief One mixer voice (see N64AudioDevice::Source - the design notes apply verbatim) */
		struct Source
		{
			bool Playing = false;
			bool Paused = false;
			bool Looping = false;
			bool Relative = true;
			float Gain = 1.0f;
			float Pitch = 1.0f;
			Vector3f Position = Vector3f(0.0f, 0.0f, 0.0f);

			std::uint32_t BufferId = 0;
			/** @brief Playback cursor in 32.32 fixed-point frames */
			std::int64_t Cursor = 0;

			std::uint32_t Queue[MaxQueuedBuffers] = {};
			std::int32_t QueueCount = 0;
			std::uint32_t Processed[MaxQueuedBuffers] = {};
			std::int32_t ProcessedCount = 0;
		};

		bool _valid;
		bool _suspended;
		/** @brief Whether this instance is the one that brought SDL's audio subsystem up, and so has to take it down */
		bool _subsystemInitialized;
		/** @brief Rate the device actually opened at, which is what the mixer resamples to */
		std::int32_t _outputFrequency;
		/** @brief SDL's device id; zero means the device never opened and everything here is inert */
		std::uint32_t _deviceId;

		/** @brief The one block the mixer fills before handing it to SDL */
		std::int16_t* _block;
		/** @brief 32-bit accumulator the sources are summed into before clamping */
		std::int32_t* _mixBuffer;

		Buffer* _buffers;
		std::int32_t _bufferCount;
		std::int32_t _bufferCapacity;

		Source _sources[MaxSources];

		SdlAudioDevice(const SdlAudioDevice&) = delete;
		SdlAudioDevice& operator=(const SdlAudioDevice&) = delete;

		Source* GetSource(std::uint32_t sourceId);
		Buffer* GetActiveBuffer(Source& source);
		void ReleaseBuffer(Buffer& buffer);

		/** @brief Tops the device's queue up to @ref TargetQueuedFrames, mixing a block at a time */
		void FillQueue();
		void MixInto(std::int16_t* output, std::int32_t frames);
		bool MixSource(Source& source, std::int32_t* output, std::int32_t frames);
		void ComputePanning(const Source& source, float& leftGain, float& rightGain) const;
		static void ReadFrame(const Buffer& buffer, std::int32_t frame, std::int32_t& left, std::int32_t& right);
	};
}

#endif
