#pragma once

#if defined(WITH_SDLAUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

namespace nCine
{
	/**
		@brief Implementation of @ref IAudioDevice on top of SDL2's audio queue

		For platforms that have a working SDL2 but no OpenAL: AmigaOS 4 and MorphOS today, where SDL2
		is the mature and maintained media layer and OpenAL is not part of the SDK at all.

		The mixer is the classic Amiga backend's, which is the N64 one before it: sources are software-
		mixed by hand into a single 16-bit stereo stream (32-bit integer accumulator, 32.32 fixed-point
		cursors, Q15 gains). What differs is only where the stream goes - blocks are handed to
		`SDL_QueueAudio()`, and how much is already waiting is read back with `SDL_GetQueuedAudioSize()`.

		The queue API is used rather than a callback deliberately. A callback would run the mixer on
		SDL's audio thread and every source operation the game makes would then need a lock around it,
		for no gain: the mixer is cheap, and running it where the rest of the frame runs keeps this
		backend the same shape as the console ones. What the queue costs instead is latency, bounded
		here by @ref TargetQueuedFrames.
	*/
	class SdlAudioDevice : public AudioDeviceBase
	{
	public:
		SdlAudioDevice();
		~SdlAudioDevice() override;

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

			The whole latency budget, and the whole headroom for a late frame - the queue is refilled once
			per rendered frame, so a frame longer than this leaves the device with nothing to play. About
			93 ms at 44.1 kHz, which covers a stretch of a level load rendered at ten frames per second
			without being long enough to hear as delay on a jump.
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
