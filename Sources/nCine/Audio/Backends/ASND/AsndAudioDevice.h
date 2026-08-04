#pragma once

#if defined(WITH_ASND) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Wii/GameCube implementation of @ref IAudioDevice on top of libogc's ASND

		devkitPro ships no OpenAL for these consoles, so this drives ASND instead: the DSP mixes up
		to 16 voices at 48 kHz, resampling and applying a per-channel volume for each of them, and
		the samples are read straight out of main memory by DMA.

		Two things the OpenAL backend gets for free have to be done here:
		- ASND has no notion of a listener or of source positions, so @ref setSourcePosition()
		  keeps the position and the mix turns it into an attenuation and a stereo panning that are
		  folded into the per-voice volume (see @ref applyVolume()).
		- ASND holds at most two buffers per voice (the one playing plus one queued), while the
		  streaming players expect an OpenAL-style queue they can push to and reclaim from. The
		  queue is therefore kept here and fed into the voice a buffer at a time by @ref pump(),
		  which runs once per frame from @ref updatePlayers().

		There is no low-pass filter on the DSP, so @ref setSourceLowPass() does nothing and sounds
		are not muffled underwater.
	*/
	class AsndAudioDevice : public AudioDeviceBase
	{
	public:
		AsndAudioDevice();
		~AsndAudioDevice() override;

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
		/** @brief Number of voices the DSP mixer provides, which is also the number of sources */
		static constexpr std::int32_t MaxSources = 16;
		/** @brief How many of a source's queued buffers ASND can hold at once (playing plus queued) */
		static constexpr std::int32_t MaxBuffersInFlight = 2;
		/** @brief Upper bound on the streaming queue of a source, @ref AudioStream uses three */
		static constexpr std::int32_t MaxQueuedBuffers = 4;
		/** @brief Output sample rate ASND fixes the DSP to */
		static constexpr std::int32_t OutputFrequency = 48000;
		/** @brief Loudest per-channel volume ASND accepts */
		static constexpr std::int32_t MaxVolume = 255;

		/** @brief Samples of one audio buffer, in a block the DSP can read by DMA */
		struct Buffer
		{
			/** @brief Sample data, 32-byte aligned and padded as ASND requires */
			std::uint8_t* data;
			/** @brief Bytes of @ref data that hold samples */
			std::int32_t size;
			/** @brief Allocated size of @ref data, which a re-upload can reuse */
			std::int32_t capacity;
			/** @brief Sample rate the samples were recorded at */
			std::int32_t frequency;
			/** @brief Matching `VOICE_*` constant of ASND */
			std::int32_t voiceFormat;
			/** @brief Whether this entry is in use */
			bool used;

			Buffer()
				: data(nullptr), size(0), capacity(0), frequency(0), voiceFormat(0), used(false) {}
		};

		/** @brief State of one voice, which is what a source is on this backend */
		struct Source
		{
			/** @brief Buffer attached for non-streamed playback, `0` if none */
			std::uint32_t attachedBufferId;
			/** @brief Streaming queue, oldest buffer first */
			std::uint32_t queuedBufferIds[MaxQueuedBuffers];
			/** @brief Entries of @ref queuedBufferIds that are in use */
			std::int32_t numQueued;
			/** @brief Leading entries of @ref queuedBufferIds that were handed to ASND */
			std::int32_t numSubmitted;
			/** @brief Leading entries of @ref queuedBufferIds that ASND has finished playing */
			std::int32_t numProcessed;

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

			/** @brief Whether the voice is being fed by the streaming queue instead of a single buffer */
			bool streaming;
			/** @brief Whether the voice has been started, i.e. whether `ASND_AddVoice()` may be used */
			bool started;
			/** @brief Whether playback is paused, which ASND reports the same way as a starved voice */
			bool paused;

			Source()
				: attachedBufferId(0), queuedBufferIds {}, numQueued(0), numSubmitted(0), numProcessed(0),
					gain(1.0f), pitch(1.0f), relative(false), looping(false), streaming(false),
					started(false), paused(false) {}
		};

		/** @brief Whether `ASND_Init()` succeeded */
		bool _initialized;
		/** @brief All buffers, the id of a buffer is its index plus one */
		SmallVector<Buffer, 0> _buffers;
		/** @brief State of every voice, indexed by voice number */
		Source _sources[MaxSources];

		/** @brief Returns the buffer of the specified id, or `nullptr` if there is none */
		Buffer* bufferForId(std::uint32_t bufferId);
		/** @brief Returns the voice number of the specified source id, or `-1` if it is not one */
		static std::int32_t voiceForId(std::uint32_t sourceId);

		/** @brief Turns the gain, position and listener of a source into a per-channel volume for ASND */
		void computeVolume(std::int32_t voice, std::int32_t& volumeLeft, std::int32_t& volumeRight);
		/** @brief Pushes a freshly computed volume into a voice that is already sounding */
		void applyVolume(std::int32_t voice);
		/** @brief Starts or restarts the voice of a source from the specified buffer */
		void startVoice(std::int32_t voice, const Buffer& buffer);
		/** @brief Moves finished buffers of every streaming voice to the processed count and submits new ones */
		void pump();
	};
}

#endif
