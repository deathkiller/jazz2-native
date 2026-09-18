#pragma once

#if defined(WITH_PS2AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

namespace nCine
{
	/**
		@brief PlayStation 2 implementation of @ref IAudioDevice on top of `audsrv`

		The SPU2 is not on the Emotion Engine's side of the machine. It hangs off the I/O Processor, and the
		only thing the EE can do with it is ask the IOP to, which is what `audsrv` is: an IRX module that owns
		the SPU2, keeps a ring buffer of PCM in IOP memory and streams it out through two of the chip's voices
		at a pitch derived from the format it was given. So this backend is a software mixer - the same one
		the Nintendo 64, PS3, Amiga, SDL and PSP backends are - whose output stage is `audsrv_play_audio()`
		rather than a DMA queue.

		<em>There is no mixer thread.</em> The engine's threading is off on this console (a thread created through
		PS2SDK's `libpthreadglue` is never scheduled, see the `NCINE_WITH_THREADS` arm in `ncine_options.cmake`),
		so the ring is topped up from @ref updatePlayers(), once per frame, on the main thread - the N64
		backend's arrangement rather than the PSP's. That works because the ring is deep: whatever `audsrv`
		was built with, it is measured at startup (see @ref _ringCapacity) and kept as full as it will go, so
		a frame that runs long is covered by what is already queued on the IOP. Nothing here ever blocks on
		the hardware: only as much is submitted as @ref audsrv_available() says will fit, which is why
		`audsrv_wait_audio()` - the call the module's own samples are written around - is never used.

		<em>The mixing rate is not the hardware's rate.</em> The SPU2 runs at 48 kHz and `audsrv` programs the voice
		pitch from the format it is handed, so a stream submitted at 22050 Hz is resampled by the SPU2 itself,
		in hardware, for free. The mixer's cost is linear in its rate and the game's content is 8- and 16-bit
		samples at 11-22 kHz, so the default is 22050 Hz and the per-source loop runs half as often as it
		would at 44100 for content that has nothing above 11 kHz to lose. @ref setMixingFrequency() reprograms
		the format (the "Sample Rate" option), and @ref nativeFrequency() reports it so the module decoders
		render at the rate they will be played at.

		Uploaded sounds keep the width they arrived in rather than being widened to the mixer's 16 bits, the
		way the N64 backend keeps them: nearly all of the game's own sounds are 8-bit, and the console's 32 MB
		is shared with a renderer that pages every texture through main memory. Unlike the N64 there is no
		decimation ladder on top of that - 32 MB is not 8 MB, and the whole sound set of a level fits.
	*/
	class Ps2AudioDevice : public AudioDeviceBase
	{
	public:
		Ps2AudioDevice();
		~Ps2AudioDevice() override;

		bool isValid() const override;
		const char* name() const override;

		void setGain(float gain) override;
		void updateListener(const Vector3f& position, const Vector3f& velocity) override;
		std::int32_t nativeFrequency() override;
		void setMixingFrequency(std::int32_t frequency) override;

		std::uint32_t registerPlayer(IAudioPlayer* player) override;
		void updatePlayers() override;

		std::uint32_t createBuffer(BufferUsage usage) override;
		void deleteBuffer(std::uint32_t bufferId) override;
		bool uploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency) override;

		void setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId) override;
		void setSourceGain(std::uint32_t sourceId, float gain) override;
		void setSourcePitch(std::uint32_t sourceId, float pitch) override;
		void setSourceLooping(std::uint32_t sourceId, bool looping) override;
		void setSourceRelative(std::uint32_t sourceId, bool relative) override;
		void setSourcePosition(std::uint32_t sourceId, const Vector3f& position) override;
		void setSourceLowPass(std::uint32_t sourceId, float value) override;
		std::int32_t sourceSampleOffset(std::uint32_t sourceId) override;
		void setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset) override;
		void playSource(std::uint32_t sourceId) override;
		void pauseSource(std::uint32_t sourceId) override;
		void stopSource(std::uint32_t sourceId) override;
		bool isSourcePlaying(std::uint32_t sourceId) override;
		void queueBuffer(std::uint32_t sourceId, std::uint32_t bufferId) override;
		std::int32_t numProcessedBuffers(std::uint32_t sourceId) override;
		void unqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds) override;

		void onBlockingOperationBegan() override;
		void onBlockingOperationEnded() override;

		void suspendDevice() override;
		void resumeDevice() override;

		/**
			@brief Brings the IOP side up - `rom0:LIBSD` and the embedded `audsrv.irx`

			An IRX can only be loaded once per process, but the device is constructed and destroyed with the
			service locator, which the application may do more than once. So this is idempotent and remembers
			what it found; the constructor calls it and then only opens a stream format on it.

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
		/** @brief Rate the sources are mixed at unless @ref setMixingFrequency() says otherwise */
		static constexpr std::int32_t DefaultMixingFrequency = 22050;
		/** @brief Bounds of what `audsrv` accepts as a stream format */
		static constexpr std::int32_t MinMixingFrequency = 8000;
		static constexpr std::int32_t MaxMixingFrequency = 48000;
		/**
			@brief Largest block the mixer will ever produce, which is what the scratch buffers are sized for

			The block actually used is smaller and derived from the ring (see @ref _blockFrames); this only
			has to be an upper bound, so a change of mixing rate never reallocates anything.
		*/
		static constexpr std::int32_t MaxBlockFrames = 1024;
		/**
			@brief Blocks the ring is meant to hold, which is what @ref _blockFrames is chosen to give

			A block is only submitted once the ring has room for a whole one, so the ring spends its time
			somewhere between full and a block short of full - which makes the block size, not the ring size,
			what decides how much audio is really queued ahead. `audsrv`'s ring turns out to be small (about
			50 ms at the default rate, measured - the module does not publish it), and a block of that order
			would leave it averaging half empty and break up on any frame that ran long. Four keeps it
			averaging seven eighths full for four RPC round trips per ring rather than one.
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
			/** @brief Width of one sample in @ref Samples, 1 or 2 (see @ref uploadBuffer()) */
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
			/** @brief Buffers played to the end, waiting to be collected by @ref unqueueBuffers() */
			std::uint32_t Processed[MaxQueuedBuffers] = {};
			std::int32_t ProcessedCount = 0;
		};

		bool _valid;
		/**
			@brief Why the module's stream is stopped, if it is - a set of @ref StopReason bits

			`audsrv_stop_audio()` does not merely silence the voices, it leaves the ring refusing everything -
			@ref audsrv_available() answers 0 from then on - until the format is programmed again, which
			resets it to the full capacity (both measured). So a stop is always paired with a
			@ref ApplyFormat(), and nothing is submitted in between.

			A set of reasons rather than one flag because two independent owners stop the same stream and
			they can overlap: a suspension arriving during a level load used to have the load's
			@ref endBlockingOperation() reopen the ring behind @ref suspendDevice()'s back, leaving the
			module streaming from a ring nobody was feeding - which on this hardware is the last block
			repeating, the very artefact @ref beginBlockingOperation() exists to prevent. The stream is open
			exactly while this is zero.
		*/
		std::uint32_t _stopReasons;
		/** @brief Rate the sources are mixed at, which is also the rate `audsrv` is streaming */
		std::int32_t _mixFrequency;
		/** @brief Frames mixed and submitted in one go, derived from the ring (see @ref TargetBlocksPerRing) */
		std::int32_t _blockFrames;
		/**
			@brief Bytes the module's ring holds when it is empty, measured rather than assumed

			`audsrv` does not publish its ring size and it has changed between PS2SDK releases, but an empty
			ring answers @ref audsrv_available() with exactly its capacity - so it is read once, right after
			the format is set, and only used to report the latency the console is actually running at.
		*/
		std::int32_t _ringCapacity;

		Buffer* _buffers;
		std::int32_t _bufferCount;
		std::int32_t _bufferCapacity;
		std::int32_t _residentBytes;

		Source _sources[MaxSources];

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

			Shared by the destructor and by the constructor's failure paths, which must not leave a
			partially built device behind: `createBuffer()` is reachable whether or not @ref isValid(), and
			it reads a null @ref _buffers as "there is no table" but cannot recognize a table of capacity 0.
		*/
		void ReleaseResources();
		/** @brief Reasons the stream can be held stopped, combined in @ref _stopReasons */
		enum StopReason : std::uint32_t {
			/** @brief A @ref beginBlockingOperation() the caller has not ended yet */
			StopReasonBlocking = 1,
			/** @brief A @ref suspendDevice() the caller has not resumed yet */
			StopReasonSuspended = 2
		};

		/** @brief Silences the module at once, discarding whatever it still holds (see @ref _stopReasons) */
		void StopStream(StopReason reason);
		/** @brief Drops @p reason and, if it was the last one, reopens the stream so the ring accepts audio */
		void RestartStream(StopReason reason);
		/**
			@brief Programs @p frequency and reopens the ring, @c false if `audsrv` refused the rate

			The half of @ref RestartStream() that actually talks to the module, shared with
			@ref setMixingFrequency() so that a rate change goes through the same state the stop reasons
			describe instead of reprogramming the ring behind them.
		*/
		bool ReopenStream(std::int32_t frequency);

		/** @brief Returns the source of an id handed out by @ref registerPlayer(), or `nullptr` */
		Source* GetSource(std::uint32_t sourceId);
		/** @brief Frees a buffer's sample data and stops anything reading it */
		void ReleaseBuffer(Buffer& buffer);
		/** @brief Returns the buffer a source is currently reading from, or `nullptr` */
		Buffer* GetActiveBuffer(Source& source);
		/** @brief Reads one interleaved frame, widening an 8-bit source to the mixer's 16-bit scale */
		static DEATH_ALWAYS_INLINE void ReadFrame(const Buffer& buffer, std::int32_t frame, std::int32_t& left, std::int32_t& right);

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
