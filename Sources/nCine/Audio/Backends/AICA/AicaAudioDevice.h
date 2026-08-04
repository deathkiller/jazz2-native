#pragma once

#if defined(WITH_AICA) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Dreamcast implementation of @ref IAudioDevice on top of the AICA

		Talks to the sound processor through KallistiOS directly instead of going through an OpenAL
		port. The AICA is a 64-channel wavetable synthesizer with **2 MB of sound RAM of its own**,
		which is what makes audio nearly free in main memory here: a fully loaded sound lives in that
		separate pool and the SH4 only ever touches it again to change a volume.

		The two kinds of player map onto two different parts of the hardware:
		- A @ref AudioBufferPlayer becomes one AICA channel @m_span{m-text m-dim} (two for a stereo
		  buffer, since a channel is mono) @m_endspan playing a sample out of sound RAM. Whether it
		  has finished is read back from the hardware with `snd_is_playing()`, so nothing has to be
		  predicted from timers.
		- A @ref AudioStreamPlayer becomes one of KallistiOS's `snd_stream` handles, whose driver on
		  the ARM keeps a ring buffer in sound RAM topped up from a callback. The callback is served
		  out of the queue this class keeps, so the streaming players still see the queue interface
		  they expect. There are only @ref MaxStreams of these.

		As with @ref AsndAudioDevice the hardware has no notion of a listener, so panning and
		distance attenuation are computed here and folded into the channel volume and pan. The AICA
		does have a low-pass filter per channel, but the driver leaves it switched off and does not
		expose it, so @ref setSourceLowPass() does nothing.
	*/
	class AicaAudioDevice : public AudioDeviceBase
	{
	public:
		AicaAudioDevice();
		~AicaAudioDevice() override;

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

		/** @brief One audio buffer, in sound RAM or in main memory depending on what it is for */
		struct Buffer
		{
			/** @brief Which of the two representations below is in use */
			BufferUsage usage;
			/** @brief Whether this entry is in use */
			bool used;

			/**
			 * @brief Sound RAM offsets of a static buffer, one per channel
			 *
			 * An AICA channel is mono, so a stereo sample is split into two blocks that two channels
			 * play in parallel. `0` means nothing is allocated.
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
				: usage(BufferUsage::Static), used(false), spuAddress {}, spuCapacity(0), data(nullptr),
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
			/** @brief Whether the hardware has been told to start */
			bool started;
			/** @brief Whether playback is paused */
			bool paused;

			Source()
				: attachedBufferId(0), channels { -1, -1 }, pausedOffset(0), streamHandle(-1),
					queuedBufferIds {}, numQueued(0), numProcessed(0), headOffset(0), gain(1.0f),
					pitch(1.0f), relative(false), looping(false), streaming(false), started(false),
					paused(false) {}
		};

		/** @brief Whether the sound processor was brought up successfully */
		bool _initialized;
		/** @brief All buffers, the id of a buffer is its index plus one */
		SmallVector<Buffer, 0> _buffers;
		/** @brief State of every source, indexed by source id minus one */
		Source _sources[MaxSources];

		/** @brief The one instance, so the C callback of the stream driver can find its way back */
		static AicaAudioDevice* _current;

		/** @brief Returns the buffer of the specified id, or `nullptr` if there is none */
		Buffer* bufferForId(std::uint32_t bufferId);
		/** @brief Returns the index of the specified source id, or `-1` if it is not one */
		static std::int32_t sourceForId(std::uint32_t sourceId);

		/** @brief Turns the gain, position and listener of a source into a volume and a panning */
		void computeVolume(std::int32_t index, bool isStereo, std::int32_t& volume, std::int32_t& pan);
		/** @brief Pushes a freshly computed volume and panning into whatever the source is playing on */
		void applyVolume(std::int32_t index);

		/** @brief Starts the attached buffer of a source on freshly allocated AICA channels */
		void startSample(std::int32_t index, std::int32_t sampleOffset);
		/** @brief Stops the AICA channels of a source and returns them to the pool */
		void stopSample(std::int32_t index);
		/** @brief Releases the stream handle of a source */
		void releaseStream(std::int32_t index);
		/** @brief Serves the stream driver from the queue of a source */
		void* fillStream(std::int32_t index, std::int32_t bytesRequested, std::int32_t& bytesProvided);

		// Spelled with plain `int` because that is what `snd_stream_callback_t` is declared with: on
		// this target `std::int32_t` is `long int`, so a signature using it would be a different type
		// and the callback could not be registered at all
		static void* streamCallback(int handle, int bytesRequested, int* bytesProvided);
	};
}

#endif
