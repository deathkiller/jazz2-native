#pragma once

#if defined(WITH_AHIAUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

struct MsgPort;
struct AHIRequest;

namespace nCine
{
	/**
		@brief Classic Amiga implementation of @ref IAudioDevice on top of ahi.device

		The mixer is the N64 backend's, transplanted: everything is software-mixed by hand into one
		16-bit stereo stream on the main thread (32-bit integer accumulator, 32.32 fixed-point cursors,
		Q15 gains), topped up once per frame from @ref updatePlayers(). What differs is only where the
		stream goes: blocks are handed to `ahi.device` as overlapping `CMD_WRITE` requests, which is the
		OS's retargetable audio - Paula's 14-bit trick on a stock machine, Pamela on a Vampire, HDMI on
		a PiStorm, a sound card where one exists - with AHI doing the final rate conversion, so the game
		mixes at the rate the performance preset can afford rather than whatever the user's AHI mode runs
		at. Where the device supports it, the block being filled is linked (`ahir_Link`) to the one still
		playing so playback is gapless - whether it does is measured rather than assumed, see
		`ProbeLinkSupport()` - and the queue is long enough to survive a late frame, see
		`_maxInFlight`.

		Unlike the N64 there is no decimation-on-upload here: the machines this port targets have tens
		of megabytes of fast RAM, so sounds are stored as they arrive (8-bit stays 8-bit, the mixer
		widens on read - that half of the N64 design is kept, because it halves the resident bytes for
		the game's mostly 8-bit content at one shift per sample).
	*/
	class AmigaAudioDevice : public AudioDeviceBase
	{
	public:
		AmigaAudioDevice();
		~AmigaAudioDevice() override;

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

		void suspendDevice() override;
		void resumeDevice() override;

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

		/** @brief One uploaded PCM buffer, interleaved, in fast RAM (see the N64 backend for the rationale) */
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

			Both follow what `ProbeLinkSupport()` found, and both spellings buffer the same ~186 ms
			at 22 kHz (~93 ms at 44.1 kHz), which is what a machine drawing ten frames a second needs
			to not run the queue dry between refills:

			- linked, fast preset: two blocks of 2048 frames. This is the documented `ahi.device` double
			  buffer - one playing, one linked behind it - and a third request has nothing to link to.
			- unlinked, fast preset: four blocks of 1024 frames. Without links the queue is just a
			  queue, but the AROS 68k device only replies a request once a *later* one has been
			  submitted, so a queue two deep never retires anything and playback stops after the first
			  two blocks. Four deep always has a successor to free the block before it.
			- slow presets: four blocks of 2048 frames, ~370 ms, and never linked whatever the probe
			  found. The queue is refilled once per rendered frame, so what it has to cover is one frame
			  time - and the machines on the slow presets are exactly the ones whose frames are long.
			  Continuity is worth more there than the seam a link would remove: a gapless queue that is
			  empty most of the time sounds far worse than a queue with a seam in it.
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

			Two silent blocks are played at startup, the second linked to the first, and the first is
			given a second to be replied. The AROS 68k device accepts a linked request, never plays it
			and never replies it, so the whole queue deadlocks after two blocks and the game runs in
			silence; the real AHI on AmigaOS 3.x replies as documented. Rather than pick one behaviour
			and be wrong on the other, the answer is measured once, here, and `FillQueue()` links
			only when linking is known to work - unlinked blocks are still played back to back, just
			without the guarantee of no gap between them.
		*/
		bool ProbeLinkSupport(std::int32_t blockBytes);

		/**
			@brief The two requests `ProbeLinkSupport()` plays its silent blocks through

			Separate from the playback requests on purpose. A device that ignores `ahir_Link` leaves the
			linked request in a state `AbortIO()` does not undo, and re-sending that same structure for
			real audio wedges the queue after a couple of blocks - so the probe gets structures of its
			own, which are never sent again once it has its answer.
		*/
		AHIRequest* _probeRequests[2];

		/**
			@brief Restarts a queue that `ahi.device` has stopped servicing

			The AROS 68k device stops replying once its queue has run dry - which a level load, where a
			single frame can take seconds, guarantees - and from then on accepts blocks without ever
			playing or returning them, so the game falls silent for good. Aborting the outstanding
			requests and starting again does clear it, so a queue that has not retired anything for a
			second while full is treated as wedged and restarted. On a device that behaves this never
			runs.
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
