#pragma once

#if defined(WITH_AICA) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Dreamcast implementation of @ref IAudioDevice on top of the AICA

		Talks to the sound processor through KallistiOS directly. Fully loaded sounds live in the
		AICA's own 2 MB of sound RAM, so they cost almost no main memory.

		- A @ref AudioBufferPlayer becomes one AICA channel (two for stereo). Its end is predicted
		  from length and rate, because real hardware never clears the key-on bit of a one-shot sample.
		- A @ref AudioStreamPlayer becomes a `snd_stream` handle fed from the queue kept here, there
		  are only @ref MaxStreams of these.
		- A sound over @ref MaxSamplesForHalving is streamed from main memory and cannot change pitch
		  once started. A sound between the two limits plays on a channel at half its rate.

		Panning and distance attenuation are computed here. @ref SetSourceLowPass() does nothing,
		the driver doesn't expose the channel filter.
	*/
	class AicaAudioDevice : public AudioDeviceBase
	{
	public:
		AicaAudioDevice();
		~AicaAudioDevice() override;

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

	protected:
		void OnBlockingOperationBegan() override;
		void OnBlockingOperationEnded() override;

	private:
		/** @brief Number of sources, comfortably below the 64 channels the AICA has */
		static constexpr std::int32_t MaxSources = 24;
		/** @brief Number of streaming players that can sound at once, the limit of `snd_stream` */
		static constexpr std::int32_t MaxStreams = 4;
		/** @brief Upper bound on the streaming queue of a source, @ref AudioStream uses three */
		static constexpr std::int32_t MaxQueuedBuffers = 4;
		/** @brief Size of the sound RAM ring buffer of each streaming channel */
		static constexpr std::int32_t StreamBufferSize = 16 * 1024;
		/** @brief Rate the module decoder renders at, the AICA resamples every channel anyway */
		static constexpr std::int32_t PreferredFrequency = 22050;
		/** @brief Loudest volume an AICA channel accepts */
		static constexpr std::int32_t MaxVolume = 255;
		/** @brief Hardware limit on the sample count of a single channel */
		static constexpr std::int32_t MaxSamplesPerChannel = 65534;
		/**
		 * @brief Milliseconds a one-shot sample is considered playing past its computed end
		 *
		 * Covers the driver's command latency and rate rounding, so the tail is not cut off.
		 */
		static constexpr std::uint64_t SampleEndMarginMs = 20;
		/**
		 * @brief Longest sound that is still played on a channel, at half its sample rate
		 *
		 * Longer sounds are streamed from main memory (see @ref StartLongSample()). Not drawn at the
		 * channel limit because the few @ref MaxStreams handles are shared with the music.
		 */
		static constexpr std::int32_t MaxSamplesForHalving = MaxSamplesPerChannel * 2;

		/** @brief One audio buffer, in sound RAM or in main memory depending on what it is for */
		struct Buffer
		{
			/** @brief Which of the two representations below is in use */
			BufferUsage usage;
			/** @brief Whether this entry is in use */
			bool used;
			/**
			 * @brief Whether this is a static buffer too long for a channel, kept in @ref data instead
			 *
			 * Played through a stream handle, see @ref StartLongSample(). 8-bit samples are kept signed
			 * and widened to 16-bit as they are handed over.
			 */
			bool longSample;

			/**
			 * @brief Sound RAM offsets of a static buffer, one per channel
			 *
			 * A channel is mono, so a stereo sample is split into two blocks. `0` means not allocated.
			 */
			std::uint32_t spuAddress[2];
			/** @brief Bytes reserved in sound RAM for each of @ref spuAddress */
			std::int32_t spuCapacity;

			/** @brief Samples of a streaming buffer, in main memory for the stream driver to read */
			std::uint8_t* data;
			/** @brief Allocated size of @ref data */
			std::int32_t capacity;
			/** @brief Bytes of @ref data that hold samples */
			std::int32_t size;

			/** @brief Number of sample frames the buffer holds */
			std::int32_t numSamples;
			/** @brief Sample rate the samples were recorded at */
			std::int32_t frequency;
			/** @brief Number of channels, 1 or 2 */
			std::int32_t numChannels;
			/** @brief Bytes per sample of one channel, 1 or 2 */
			std::int32_t bytesPerSample;

			Buffer()
				: usage(BufferUsage::Static), used(false), longSample(false), spuAddress {}, spuCapacity(0), data(nullptr),
					capacity(0), size(0), numSamples(0), frequency(0), numChannels(0), bytesPerSample(0) {}
		};

		/** @brief State of one source, which becomes either AICA channels or a stream handle */
		struct Source
		{
			/** @brief Buffer attached for non-streamed playback, `0` if none */
			std::uint32_t attachedBufferId;
			/** @brief AICA channels the attached buffer is playing on, `-1` when not allocated */
			std::int32_t channels[2];
			/** @brief Playback position kept across a pause, in sample frames */
			std::int32_t pausedOffset;
			/**
			 * @brief Sample frame the channels were keyed on at
			 *
			 * A channel always starts at its base address, so positions the hardware reports are
			 * relative to this.
			 */
			std::int32_t sampleStartOffset;
			/**
			 * @brief Time a one-shot sample will have played out, in the millisecond clock, `0` if none
			 *
			 * Set when the channels are keyed on and moved when the pitch changes, see @ref IsSourcePlaying().
			 */
			std::uint64_t sampleEndAt;

			/** @brief Stream handle of a streaming source, `-1` when it has none */
			std::int32_t streamHandle;
			/** @brief Streaming queue, oldest buffer first */
			std::uint32_t queuedBufferIds[MaxQueuedBuffers];
			/** @brief Entries of @ref queuedBufferIds that are in use */
			std::int32_t numQueued;
			/** @brief Leading entries of @ref queuedBufferIds the stream driver has consumed */
			std::int32_t numProcessed;
			/** @brief Bytes of the oldest queued buffer that have already been handed to the driver */
			std::int32_t headOffset;

			/** @brief Player gain */
			float gain;
			/** @brief Playback rate multiplier */
			float pitch;
			/** @brief Position in physical units, as handed over by the player */
			Vector3f position;
			/** @brief Whether the position is relative to the listener */
			bool relative;
			/** @brief Whether a non-streamed buffer repeats */
			bool looping;

			/** @brief Whether the source is fed by the streaming queue instead of a single buffer */
			bool streaming;
			/** @brief Whether the stream handle is fed from the attached long sample instead of the queue */
			bool longSample;
			/** @brief Next sample frame of the attached long sample to hand to the stream driver */
			std::int32_t longSamplePos;
			/** @brief Which half of the staging block the next request of an 8-bit long sample is written to */
			std::int32_t longSampleStagingHalf;
			/**
			 * @brief Time the last sample frame of a long sample was handed to the driver, 0 while playing
			 *
			 * The sound is over only once the driver's ring buffer has played out, see @ref IsSourcePlaying().
			 */
			std::uint64_t longSampleDrainedAt;
			/** @brief Whether the hardware has been told to start */
			bool started;
			/** @brief Whether playback is paused */
			bool paused;
			/**
			 * @brief Whether a stream was stopped for a blocking operation and is to be started again after it
			 *
			 * See @ref OnBlockingOperationBegan(). A source in this state still reports itself as playing.
			 */
			bool stoppedForBlocking;

			Source()
				: attachedBufferId(0), channels { -1, -1 }, pausedOffset(0), sampleStartOffset(0), sampleEndAt(0), streamHandle(-1),
					queuedBufferIds {}, numQueued(0), numProcessed(0), headOffset(0), gain(1.0f),
					pitch(1.0f), relative(false), looping(false), streaming(false), longSample(false),
					longSamplePos(0), longSampleStagingHalf(0), longSampleDrainedAt(0), started(false), paused(false),
					stoppedForBlocking(false) {}
		};

		/** @brief Whether the sound processor was brought up successfully */
		bool _initialized;
		/** @brief Whether a blocking operation is in progress, so no stream is started until it ends */
		bool _inBlockingOperation;
		/** @brief All buffers, the id of a buffer is its index plus one */
		SmallVector<Buffer, 0> _buffers;
		/** @brief State of every source, indexed by source id minus one */
		Source _sources[MaxSources];

		/**
		 * @brief Staging blocks a long 8-bit sample is expanded to 16-bit into, one pair per stream handle
		 *
		 * The driver DMAs a block after the callback returns, so two halves are alternated to never
		 * overwrite one still being read. Allocated on first use, @ref LongSampleStagingSize bytes each.
		 */
		std::uint8_t* _longSampleStaging[MaxStreams];
		/** @brief Size of each of @ref _longSampleStaging, two blocks of the most the driver asks for at once */
		static constexpr std::int32_t LongSampleStagingSize = StreamBufferSize;

		/** @brief The one instance, so the C callback of the stream driver can find its way back */
		static AicaAudioDevice* _current;

		/** @brief Returns the buffer of the specified id, or `nullptr` if there is none */
		Buffer* FindBuffer(std::uint32_t bufferId);
		/** @brief Returns the index of the specified source id, or `-1` if it is not one */
		static std::int32_t GetSourceIndex(std::uint32_t sourceId);

		/** @brief Turns the gain, position and listener of a source into a volume and a panning */
		void ComputeVolume(std::int32_t index, bool isStereo, std::int32_t& volume, std::int32_t& pan);
		/** @brief Pushes a freshly computed volume and panning into whatever the source is playing on */
		void ApplyVolume(std::int32_t index);

		/** @brief Starts the attached buffer of a source on freshly allocated AICA channels */
		void StartSample(std::int32_t index, std::int32_t sampleOffset);
		/** @brief Stops the AICA channels of a source and returns them to the pool */
		void StopSample(std::int32_t index);
		/** @brief Starts the stream of a queue-fed source, once it has something queued */
		void StartStream(std::int32_t index);
		/** @brief Starts the attached long sample of a source on a freshly allocated stream handle */
		void StartLongSample(std::int32_t index, std::int32_t sampleOffset);
		/** @brief Stops the stream a long sample is playing through and releases its handle */
		void StopLongSample(std::int32_t index);
		/** @brief Releases the stream handle of a source */
		void ReleaseStream(std::int32_t index);
		/** @brief Serves the stream driver from the queue of a source */
		void* FillStream(std::int32_t index, std::int32_t bytesRequested, std::int32_t& bytesProvided);

		// Plain `int` to match `snd_stream_callback_t`, `std::int32_t` is `long int` on this target
		static void* StreamCallback(int handle, int bytesRequested, int* bytesProvided);
	};
}

#endif
