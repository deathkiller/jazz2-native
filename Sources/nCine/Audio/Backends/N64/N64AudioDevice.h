#pragma once

#if defined(WITH_N64AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

namespace nCine
{
	/**
		@brief Nintendo 64 implementation of @ref IAudioDevice on top of libdragon's AI driver

		The console has no sound chip. Where the Dreamcast's AICA is a 64-voice wavetable part and the
		Wii's DSP runs libogc's ASND, the N64's Audio Interface is a bare stereo DAC that DMAs 16-bit
		interleaved samples out of RDRAM and nothing else - no voices, no volume registers, no filters.
		libdragon wraps it as a short queue of fixed-size buffers; everything above that - mixing,
		resampling, panning, looping, stream queues - is this class, on the VR4300.

		The design is the PS3 backend's, shaped by the same two constraints. *Everything is mixed by
		hand into one output*, so @ref setSourceGain() and friends only record a value that the mixer
		applies while it walks the sources, and a silent source costs nothing. *There is no mixer
		thread* - the queue is topped up from @ref updatePlayers(), once per frame, on the main thread,
		which works because @ref audio_init() is asked for enough headroom to survive a late frame
		(see the constructor).

		Three things differ from the PS3. The accumulator is 32-bit integer rather than float: the AI
		takes 16-bit samples anyway, and on a 93 MHz CPU the integer path leaves the FPU to the game.
		Nothing is byte-swapped anywhere - the mix is done in native (big-endian) integers and the AI
		reads them back natively, while the asset readers already produced native-endian samples.

		And *sample memory is the scarcest thing this backend touches*, which shapes three decisions the
		PS3 does not have to make - all of them in @ref uploadBuffer().

		Uploaded sounds keep the width they arrived in rather than being widened to the mixer's 16
		bits, because the game's own content is almost entirely 8-bit: the intro cinematic's 38 samples
		are 1.9 MB at their own width and 3.8 MB widened, out of 8 MB of RDRAM for the whole console.

		A sound past @ref MaxBytesPerBuffer is stored at a lower sample rate instead of in full. This
		is the one lever the backend has that costs no sound: the mixer resamples everything anyway, so
		a reduced sound still plays at its pitch and for its length and only loses its high end. It is
		deliberately *not* an eviction policy. Nothing here can tell whether the game still holds an
		@ref AudioBuffer for a given id, the engine uploads a sound once and never re-uploads it, and
		so throwing one away would silence it permanently and pick its victim by load order. Losing
		bandwidth predictably beats losing whole sounds unpredictably.

		And every allocation is checked: a sound that does not fit is refused, logged with the size and
		the free heap, and degrades to silence, because the alternative on this platform is the heap
		failing under some unrelated allocation a frame later. That check is also the only bound on the
		total - there is no fixed sample budget on top of it, because @ref MinFreeHeapBytes already
		yields against the heap as it actually is, which lets audio use memory a menu has spare and
		give it back where a level does not.
	*/
	class N64AudioDevice : public AudioDeviceBase
	{
	public:
		N64AudioDevice();
		~N64AudioDevice() override;

		bool isValid() const override;

		const char* name() const override;

		void setGain(float gain) override;

		void updateListener(const Vector3f& position, const Vector3f& velocity) override;

		std::int32_t nativeFrequency() override;

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
		/** @brief Sources the mixer walks; a silent one costs nothing, so this is generous */
		static constexpr std::int32_t MaxSources = 32;
		/** @brief Upper bound on the streaming queue of a source (@ref AudioStream uses three) */
		static constexpr std::int32_t MaxQueuedBuffers = 4;
		/** @brief Channels the AI scans out; the hardware is stereo-only */
		static constexpr std::int32_t ChannelCount = 2;
		/**
			@brief Rate the AI is asked for; every source is resampled to what it actually grants

			The same 48 kHz the PS3 backend fixes, so the two consoles mix the same content the same
			way. The AI derives its rate from the video clock through an integer divider, so the
			granted rate lands near this rather than on it - @ref _outputFrequency holds the truth.
		*/
		static constexpr std::int32_t OutputFrequency = 48000;
		/**
			@brief Free heap an upload leaves behind, or it is refused

			Sample data is the one thing the game loads that can be told "no" without anything else
			failing, so it yields first: letting an upload through costs whatever allocates next - a
			texture, a tilemap - which then fails somewhere with no idea that audio took the memory.

			The number is small on purpose. The check that actually decides whether a sound fits is
			@ref std::malloc() returning `nullptr`, which is exact; this is only a courtesy margin so
			audio does not take the last crumbs and a heuristic that produces a better log line than a
			failed allocation elsewhere would. Sized for a burst of ordinary in-level allocations -
			actors, tilemap chunks, particles, a few KB apiece - and no larger, because in a level the
			whole heap has around half a megabyte free: a reserve of that order does not protect
			anything, it just refuses every sound the level streams in. Scaling it by "what the level
			needs" was considered and rejected - nothing inside an audio backend knows that number, and
			a fabricated formula would only dress up the same guess.
		*/
		static constexpr std::int32_t MinFreeHeapBytes = 64 * 1024;
		/**
			@brief Bytes one sound may hold before it is stored at a lower sample rate

			Set at the top of the content's own distribution rather than at a round number. Across the
			sound sets a level draws on, 208 of 209 samples are at most 120 KB and most are under 8 KB;
			exactly one - a 21 second jingle - is 475 KB, four times the next largest. A cap just above
			that 120 KB leaves every ordinary sound untouched at full quality and reaches only genuine
			outliers, which is the whole intent: this trades bandwidth for bytes, so it should be spent
			only where the bytes are actually concentrated.
		*/
		static constexpr std::int32_t MaxBytesPerBuffer = 128 * 1024;
		/**
			@brief Rate below which @ref MaxBytesPerBuffer stops being enforced

			A floor on how far the trade above may go, so a very long sound is stored large rather than
			destroyed. Roughly where the game's own content already sits at its low end - it ships
			effects authored at 4-5 kHz - so a sound reduced to this is no worse than what JJ2 itself
			considered acceptable for its longer ambiences.
		*/
		static constexpr std::int32_t MinDecimatedFrequency = 5000;
		/**
			@brief Footprint change between log lines

			Sample memory is loaded and released a sound at a time but matters in aggregate, so it is
			reported each time it moves a step of this size in either direction rather than per upload.
			A level's worth of sounds is then a handful of lines that show the trajectory, which is what
			a memory problem under churn needs, instead of two hundred that hide it. Reporting the
			releases and not just the loads is the point: a footprint that only ever grows and one that
			is handed back between levels look identical if only growth is logged.
		*/
		static constexpr std::int32_t ResidentLogStep = 128 * 1024;
		/** @brief Entries the buffer table starts with, enough that a cinematic never grows it */
		static constexpr std::int32_t InitialBufferCapacity = 64;
		/** @brief AI buffers one frame may mix, so a late frame cannot spend itself catching up (see @ref FillQueue()) */
		static constexpr std::int32_t MaxBuffersPerFill = 4;

		/**
			@brief One uploaded PCM buffer, interleaved, in RDRAM

			Allocated with @ref std::malloc() rather than held in a container, the way the Dreamcast
			backend holds its own: the allocation has to be checked, reused across the re-uploads a
			streaming source performs, and released deterministically, and a raw pointer says all
			three plainly.
		*/
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
			std::int32_t Frequency = OutputFrequency;
			/** @brief Frames (sample pairs for stereo), which is what the mixer's cursor counts in */
			std::int32_t FrameCount = 0;
			/**
				@brief How many source frames one stored frame stands for (1 = stored as uploaded)

				An oversized sound is stored at a reduced rate (see @ref uploadBuffer()); the mixer never
				has to know - @ref Frequency already reflects it - but the sample-offset accessors do,
				because their callers speak in the ORIGINAL sound's sample space.
			*/
			std::int32_t Decimation = 1;
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
			/**
				@brief Playback cursor in 32.32 fixed-point frames

				Fixed point rather than floating: the resampler advances it once per OUTPUT SAMPLE, and on
				the VR4300's single unpipelined FPU the double compare/convert/add per sample was measurable
				across the frame's few thousand samples - a 64-bit integer add is one pipelined instruction.
				The 32 fraction bits also never lose sub-sample precision, however long a stream runs
				(a double's mantissa would, only much later).
			*/
			std::int64_t Cursor = 0;

			/** @brief Buffers queued on a streaming source, oldest first */
			std::uint32_t Queue[MaxQueuedBuffers] = {};
			std::int32_t QueueCount = 0;
			/** @brief Buffers played to the end, waiting to be collected by @ref unqueueBuffers() */
			std::uint32_t Processed[MaxQueuedBuffers] = {};
			std::int32_t ProcessedCount = 0;
		};

		bool _valid;
		bool _suspended;
		/** @brief Rate the AI actually granted, which its video-derived clock rounds to */
		std::int32_t _outputFrequency;
		/** @brief Frames in one AI buffer, sized by libdragon from the rate and the headroom */
		std::int32_t _bufferFrames;
		/** @brief Bytes of sample data currently allocated, for the log to attribute the heap by */
		std::int32_t _residentBytes;
		/** @brief Footprint the last @ref ResidentLogStep report was made at */
		std::int32_t _residentLogged;

		/**
			@brief Buffer table, index 0 reserved as "no buffer"

			Grown by hand for the same reason the samples are allocated by hand: a container that
			reallocates itself has nowhere to report a failure to, and the growth it attempted has
			already replaced the only pointer to the old table by the time the caller could look.
		*/
		Buffer* _buffers;
		std::int32_t _bufferCount;
		std::int32_t _bufferCapacity;

		Source _sources[MaxSources];

		/**
			@brief Scratch the frame's audio is accumulated into before it is clamped into an AI buffer

			32-bit so that a loud moment can exceed the 16-bit range while sources are still being
			summed; the excess is clamped once at the end rather than folded over on every addition.
			Allocated once at startup and never resized, so the mixer never allocates.
		*/
		std::int32_t* _mixBuffer;

		/** @brief Returns the source of an id handed out by @ref registerPlayer(), or `nullptr` */
		Source* GetSource(std::uint32_t sourceId);

		/** @brief Frees a buffer's sample data and stops anything reading it */
		void ReleaseBuffer(Buffer& buffer);
		/** @brief Reports the sample footprint once it has moved by @ref ResidentLogStep either way */
		void ReportResidentBytes();
		/** @brief Number of buffers currently holding sample data */
		std::int32_t CountLoadedBuffers() const;
		/** @brief Reads one interleaved frame, widening an 8-bit source to the mixer's 16-bit scale */
		static DEATH_ALWAYS_INLINE void ReadFrame(const Buffer& buffer, std::int32_t frame, std::int32_t& left, std::int32_t& right);

		/** @brief Tops the AI queue up, bounded and never blocking on the hardware */
		void FillQueue();
		/** @brief Mixes @p frames of every playing source and clamps the sum into @p output */
		void MixInto(std::int16_t* output, std::int32_t frames);
		/**
			@brief Mixes one source, advancing its cursor and retiring its buffers

			@returns `false` once the source has run out of audio, which stops it
		*/
		bool MixSource(Source& source, std::int32_t* output, std::int32_t frames);
		/** @brief Returns the buffer a source is currently reading from, or `nullptr` */
		Buffer* GetActiveBuffer(Source& source);
		/** @brief Computes the left/right gains of a source from its position and the listener's */
		void ComputePanning(const Source& source, float& leftGain, float& rightGain) const;
	};
}

#endif
