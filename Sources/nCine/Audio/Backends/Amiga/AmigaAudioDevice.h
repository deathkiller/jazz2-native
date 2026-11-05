#pragma once

#if defined(WITH_AHIAUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

struct MsgPort;
struct AHIRequest;

namespace nCine
{
	/**
		@brief Classic Amiga implementation of @ref IAudioDevice on top of ahi.device

		Everything is software-mixed on the main thread into one 16-bit stereo stream (integer accumulator,
		32.32 fixed-point cursors, Q15 gains), topped up once per frame from @ref UpdatePlayers(). Blocks go
		to `ahi.device` as `CMD_WRITE` requests, linked for gapless playback where the device supports it
		(see `ProbeLinkSupport()`), and AHI converts to its mode's rate. Samples keep their original width.
	*/
	class AmigaAudioDevice : public AudioDeviceBase
	{
	public:
		AmigaAudioDevice();
		~AmigaAudioDevice() override;

		bool IsValid() const override;

		const char* GetName() const override;

		void SetGain(float gain) override;

		void UpdateListener(const Vector3f& position, const Vector3f& velocity) override;

		std::int32_t GetNativeFrequency() override;
		void SetMixingFrequency(std::int32_t frequency) override;

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
		/** @brief AHI is asked for stereo; a mono mode gets AHI's own downmix */
		static constexpr std::int32_t ChannelCount = 2;
		/** @brief Frames a block is allocated for; the queue actually uses `_blockFrames` of them */
		static constexpr std::int32_t MaxBlockFrames = 2048;
		/** @brief Mixing blocks the device rotates through; at most `_maxInFlight` are playing at once */
		static constexpr std::int32_t BlockCount = 4;
		/** @brief Entries the buffer table starts with */
		static constexpr std::int32_t InitialBufferCapacity = 64;

		/** @brief One uploaded PCM buffer, interleaved, in fast RAM */
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

		/** @brief One mixer voice */
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
		/** @brief Rate the game mixes at, chosen by the performance preset; AHI converts to its mode's rate */
		std::int32_t _outputFrequency;

		MsgPort* _replyPort;
		/** @brief The request the device was opened with (index 0) and its clones, one per block */
		AHIRequest* _requests[BlockCount];
		/** @brief Sample blocks the requests point at */
		std::int16_t* _blocks[BlockCount];
		/** @brief Whether a request is currently queued on the device */
		bool _inFlight[BlockCount];
		/** @brief The last request sent, for ahir_Link gapless chaining */
		AHIRequest* _lastQueued;
		bool _deviceOpen;
		/** @brief Whether this `ahi.device` actually replies a linked request, see `ProbeLinkSupport()` */
		bool _linkSupported;
		/**
			@brief Frames one CMD_WRITE block carries, and how many may be outstanding

			Linked: two blocks of 2048 frames (the documented double buffer). Unlinked: four blocks of 1024
			frames, because the AROS 68k device only replies a request once a later one is submitted, so a
			queue two deep stalls. Slow presets: four blocks of 2048 frames, never linked, to cover long frames.
		*/
		std::int32_t _blockFrames;
		std::int32_t _maxInFlight;

		Buffer* _buffers;
		std::int32_t _bufferCount;
		std::int32_t _bufferCapacity;

		Source _sources[MaxSources];

		/** @brief 32-bit accumulator scratch, one frame's block */
		std::int32_t* _mixBuffer;

		Source* GetSource(std::uint32_t sourceId);

		void ReleaseBuffer(Buffer& buffer);

		/** @brief Collects finished requests and sends freshly mixed blocks, never blocking */
		void FillQueue();
		/**
			@brief Finds out whether `ahir_Link` works on this `ahi.device`

			Plays two silent blocks, the second linked to the first, and waits up to a second for a reply.
			The AROS 68k device never replies a linked request, which deadlocks the queue, so `FillQueue()`
			links only if this succeeds.
		*/
		bool ProbeLinkSupport(std::int32_t blockBytes);

		/**
			@brief The two requests `ProbeLinkSupport()` plays its silent blocks through

			Kept separate and never reused: a device that ignores `ahir_Link` leaves the linked request in a
			state `AbortIO()` does not undo, and re-sending it wedges the queue.
		*/
		AHIRequest* _probeRequests[2];

		/**
			@brief Restarts a queue that `ahi.device` has stopped servicing

			The AROS 68k device stops servicing a queue that ran dry (e.g. during a level load). A full queue
			that retires nothing for a second is aborted and restarted.
		*/
		void RecoverStalledQueue();

		/** @brief EClock value at the last retired block, the input to the stall test above */
		std::uint64_t _lastRetireTicks;
		void MixInto(std::int16_t* output, std::int32_t frames);
		bool MixSource(Source& source, std::int32_t* output, std::int32_t frames);
		Buffer* GetActiveBuffer(Source& source);
		void ComputePanning(const Source& source, float& leftGain, float& rightGain) const;
		static DEATH_ALWAYS_INLINE void ReadFrame(const Buffer& buffer, std::int32_t frame, std::int32_t& left, std::int32_t& right);
	};
}

#endif
