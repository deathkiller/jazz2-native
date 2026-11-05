#pragma once

#if defined(WITH_PS2AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

namespace nCine
{
	/**
		@brief PlayStation 2 implementation of @ref IAudioDevice on top of `audsrv`

		Software mixer whose output goes through `audsrv_play_audio()` into the ring `audsrv` keeps on the IOP.
		Threads are off on this console, so @ref UpdatePlayers() tops the ring up once per frame without ever
		blocking, and music chunks are decoded in slices from @ref RunIdleWork() while the frame waits for the
		vertical blank. Mixes at 22050 Hz by default; the SPU2 resamples to 48 kHz in hardware.
	*/
	class Ps2AudioDevice : public AudioDeviceBase
	{
	public:
		Ps2AudioDevice();
		~Ps2AudioDevice() override;

		bool IsValid() const override;
		const char* GetName() const override;

		void SetGain(float gain) override;
		void UpdateListener(const Vector3f& position, const Vector3f& velocity) override;
		std::int32_t GetNativeFrequency() override;
		void SetMixingFrequency(std::int32_t frequency) override;

		std::uint32_t RegisterPlayer(IAudioPlayer* player) override;
		void UpdatePlayers() override;
		bool SubmitStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request) override;
		void DrainStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request) override;
		bool RunIdleWork(float availableMs) override;

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

		void OnBlockingOperationBegan() override;
		void OnBlockingOperationEnded() override;

		void SuspendDevice() override;
		void ResumeDevice() override;

		/**
			@brief Brings the IOP side up - `rom0:LIBSD` and the embedded `audsrv.irx`

			Idempotent, because an IRX can only be loaded once per process but the device may be created more than once.

			@returns `false` if the modules could not be loaded, which makes the backend report itself invalid
		*/
		static bool InitializeModules();

	private:
		/** @brief Sources the mixer walks; a silent one costs nothing, so this is generous */
		static constexpr std::int32_t MaxSources = 32;
		/** @brief Upper bound on the streaming queue of a source (@ref AudioStream uses three) */
		static constexpr std::int32_t MaxQueuedBuffers = 4;
		/** @brief The SPU2 plays stereo and `audsrv` streams stereo */
		static constexpr std::int32_t ChannelCount = 2;
		/** @brief Rate the sources are mixed at unless @ref SetMixingFrequency() says otherwise */
		static constexpr std::int32_t DefaultMixingFrequency = 22050;
		/** @brief Bounds of what `audsrv` accepts as a stream format */
		static constexpr std::int32_t MinMixingFrequency = 8000;
		static constexpr std::int32_t MaxMixingFrequency = 48000;
		/**
			@brief Largest block the mixer will ever produce, which is what the scratch buffers are sized for

			Only an upper bound, the block actually used is @ref _blockFrames, so a rate change never reallocates.
		*/
		static constexpr std::int32_t MaxBlockFrames = 1024;
		/**
			@brief Blocks the ring is meant to hold, which is what @ref _blockFrames is chosen to give

			A block is only submitted once a whole one fits, so the block size decides how full the ring stays.
			The ring is small (about 50 ms), and four blocks keep it about seven eighths full.
		*/
		static constexpr std::int32_t TargetBlocksPerRing = 4;
		/** @brief Granularity @ref _blockFrames is rounded to, and its floor */
		static constexpr std::int32_t BlockFramesUnit = 64;
		/** @brief Blocks one frame may mix, so a late frame cannot spend itself catching up (see @ref FillRing()) */
		static constexpr std::int32_t MaxBlocksPerFill = 8;
		/** @brief Entries the buffer table starts with, enough that a cinematic never grows it */
		static constexpr std::int32_t InitialBufferCapacity = 64;

		/** @brief One uploaded PCM buffer, interleaved, in main memory */
		struct Buffer
		{
			bool Used = false;
			/** @brief Owning pointer to the samples, or `nullptr` */
			std::uint8_t* Samples = nullptr;
			/** @brief Bytes actually allocated, kept so a re-upload of the same size does not churn the heap */
			std::int32_t Capacity = 0;
			/** @brief Width of one sample in @ref Samples, 1 or 2 (see @ref UploadBuffer()) */
			std::int32_t BytesPerSample = 2;
			std::int32_t ChannelCount = 1;
			std::int32_t Frequency = DefaultMixingFrequency;
			/** @brief Frames (sample pairs for stereo), which is what the mixer's cursor counts in */
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

			/** @brief Buffer a static source plays, or 0 */
			std::uint32_t BufferId = 0;
			/** @brief Playback cursor in 32.32 fixed-point frames, so the resampler never touches the FPU */
			std::int64_t Cursor = 0;

			/** @brief Buffers queued on a streaming source, oldest first */
			std::uint32_t Queue[MaxQueuedBuffers] = {};
			std::int32_t QueueCount = 0;
			/** @brief Buffers played to the end, waiting to be collected by @ref UnqueueBuffers() */
			std::uint32_t Processed[MaxQueuedBuffers] = {};
			std::int32_t ProcessedCount = 0;
		};

		bool _valid;
		/**
			@brief Why the module's stream is stopped, if it is - a set of @ref StopReason bits

			After `audsrv_stop_audio()` the ring refuses everything until the format is programmed again, so
			every stop is paired with @ref ApplyFormat(). A set because blocking operations and suspension stop
			the stream independently and can overlap; the stream is open exactly while this is zero.
		*/
		std::uint32_t _stopReasons;
		/** @brief Rate the sources are mixed at, which is also the rate `audsrv` is streaming */
		std::int32_t _mixFrequency;
		/** @brief Frames mixed and submitted in one go, derived from the ring (see @ref TargetBlocksPerRing) */
		std::int32_t _blockFrames;
		/**
			@brief Bytes the module's ring holds when it is empty, measured rather than assumed

			`audsrv` does not publish it, but an empty ring answers @ref audsrv_available() with its capacity.
		*/
		std::int32_t _ringCapacity;

		Buffer* _buffers;
		std::int32_t _bufferCount;
		std::int32_t _bufferCapacity;
		std::int32_t _residentBytes;

		Source _sources[MaxSources];

		/**
			@brief Bytes a slice of the idle decode adds to a chunk, 256 frames of 16-bit stereo

			libxmp renders a whole tick at a time, so a slice costs either almost nothing or a full tick,
			hence the slowly decaying cost estimate.
		*/
		static constexpr std::int32_t IdleDecodeSliceBytes = 1024;
		/**
			@brief Frames a chunk may take in the idle time before the frame itself advances it

			The music would only gap after about 30 frames, so the frame takes over with room to spare.
		*/
		static constexpr std::uint32_t IdleDecodeGraceFrames = 12;
		/** @brief Bytes the frame adds to a chunk that is past its grace, 512 frames */
		static constexpr std::int32_t InFrameDecodeSliceBytes = 2048;

		/** @brief A stream chunk being decoded in the idle time, oldest first */
		struct IdleDecode
		{
			std::shared_ptr<StreamDecodeRequest> Request;
			/** @brief Value of @ref _frameIndex when it was submitted */
			std::uint32_t SubmittedFrame;
		};

		SmallVector<IdleDecode, 4> _idleDecodes;
		/** @brief Counts @ref UpdatePlayers() calls, which is once per frame */
		std::uint32_t _frameIndex;
		/** @brief What a slice of the idle decode took lately, in milliseconds - a slice is only started if it fits */
		float _idleSliceMs;

		/** @brief Scratch the block is accumulated into, 32-bit so the sum can exceed the output range */
		std::int32_t* _mixBuffer;
		/** @brief The block handed to `audsrv`, aligned so the SIF transfer out of it is not split */
		std::int16_t* _block;

		Ps2AudioDevice(const Ps2AudioDevice&) = delete;
		Ps2AudioDevice& operator=(const Ps2AudioDevice&) = delete;

		/** @brief Programs the stream format and measures the ring, @c false if `audsrv` refused the rate */
		bool ApplyFormat(std::int32_t frequency);
		/**
			@brief Frees the buffer table and the scratch buffers, leaving every pointer null

			Also used by the constructor's failure paths: `CreateBuffer()` runs even if not @ref IsValid() and
			only recognizes a null @ref _buffers as "no table", not a table of capacity 0.
		*/
		void ReleaseResources();
		/** @brief Reasons the stream can be held stopped, combined in @ref _stopReasons */
		enum StopReason : std::uint32_t {
			/** @brief A @ref BeginBlockingOperation() the caller has not ended yet */
			StopReasonBlocking = 1,
			/** @brief A @ref SuspendDevice() the caller has not resumed yet */
			StopReasonSuspended = 2
		};

		/** @brief Silences the module at once, discarding whatever it still holds (see @ref _stopReasons) */
		void StopStream(StopReason reason);
		/** @brief Drops @p reason and, if it was the last one, reopens the stream so the ring accepts audio */
		void RestartStream(StopReason reason);
		/**
			@brief Programs @p frequency and reopens the ring, @c false if `audsrv` refused the rate

			Shared by @ref RestartStream() and @ref SetMixingFrequency(), so a rate change respects the stop reasons.
		*/
		bool ReopenStream(std::int32_t frequency);

		/** @brief Returns the source of an id handed out by @ref RegisterPlayer(), or `nullptr` */
		Source* GetSource(std::uint32_t sourceId);
		/** @brief Frees a buffer's sample data and stops anything reading it */
		void ReleaseBuffer(Buffer& buffer);
		/** @brief Returns the buffer a source is currently reading from, or `nullptr` */
		Buffer* GetActiveBuffer(Source& source);

		/** @brief Tops the module's ring up, bounded and never blocking on the hardware */
		void FillRing();
		/** @brief Mixes @p frames of every playing source and clamps the sum into @p output */
		void MixInto(std::int16_t* output, std::int32_t frames);
		/**
			@brief Mixes one source, advancing its cursor and retiring its buffers

			@returns `false` once the source has run out of audio, which stops it
		*/
		bool MixSource(Source& source, std::int32_t* output, std::int32_t frames);
		/** @brief Computes the left/right gains of a source from its position and the listener's */
		void ComputePanning(const Source& source, float& leftGain, float& rightGain) const;
	};
}

#endif
