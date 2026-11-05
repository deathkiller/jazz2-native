#pragma once

#if defined(WITH_ASND) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Wii/GameCube implementation of @ref IAudioDevice on top of libogc's ASND

		The DSP mixes up to 16 voices at 48 kHz, reading samples from main memory by DMA.

		- Positions are turned into attenuation and panning here, see @ref ApplyVolume().
		- ASND holds only two buffers per voice, so the streaming queue is kept here and fed into
		  the voice by @ref Pump() once per frame.

		The DSP has no low-pass filter, so @ref SetSourceLowPass() does nothing.
	*/
	class AsndAudioDevice : public AudioDeviceBase
	{
	public:
		AsndAudioDevice();
		~AsndAudioDevice() override;

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
		Buffer* FindBuffer(std::uint32_t bufferId);
		/** @brief Returns the voice number of the specified source id, or `-1` if it is not one */
		static std::int32_t GetVoiceIndex(std::uint32_t sourceId);

		/** @brief Turns the gain, position and listener of a source into a per-channel volume for ASND */
		void ComputeVolume(std::int32_t voice, std::int32_t& volumeLeft, std::int32_t& volumeRight);
		/** @brief Pushes a freshly computed volume into a voice that is already sounding */
		void ApplyVolume(std::int32_t voice);
		/** @brief Starts or restarts the voice of a source from the specified buffer */
		void StartVoice(std::int32_t voice, const Buffer& buffer);
		/** @brief Moves finished buffers of every streaming voice to the processed count and submits new ones */
		void Pump();
	};
}

#endif
