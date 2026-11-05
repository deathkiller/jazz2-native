#pragma once

#if defined(WITH_PSPAUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

#include <atomic>

namespace nCine
{
	/**
		@brief Implementation of @ref IAudioDevice on top of the PSP's `sceAudio` hardware channel

		Uses the SDL backend's integer mixer instead of pspdev's OpenAL, which costs too much CPU here. Each
		block is mixed on a thread above the game's priority, so a long frame causes no dropout; shared state
		is guarded by a kernel semaphore, because spinning would never let the main thread release it.
		The hardware plays only 44100 Hz, a lower mixing rate (see @ref SetMixingFrequency()) is upsampled.

		The mixer thread must never retry a failed output without waiting, or it starves the main thread. It
		stays out of `sceAudio` while the console sleeps (see `Backends::PspPower`) and reserves a new channel
		after every wake.
	*/
	class PspAudioDevice : public AudioDeviceBase
	{
	public:
		PspAudioDevice();
		~PspAudioDevice() override;

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
		static constexpr std::int32_t MaxSources = 32;
		static constexpr std::int32_t MaxQueuedBuffers = 4;
		static constexpr std::int32_t ChannelCount = 2;
		/** @brief The hardware's native rate; `sceAudioChReserve()` channels play nothing else */
		static constexpr std::int32_t OutputFrequency = 44100;
		/** @brief Rate the sources are mixed at unless @ref SetMixingFrequency() says otherwise (see the class documentation) */
		static constexpr std::int32_t DefaultMixingFrequency = 44100;
		/**
			@brief Frames per hardware block (a multiple of 64, as `sceAudioChReserve()` requires) - 23 ms

			Also the latency of a newly started sound.
		*/
		static constexpr std::int32_t BlockFrames = 1024;
		static constexpr std::int32_t InitialBufferCapacity = 64;
		/** @brief How long the mixer thread waits after a failed output call, or while the console sleeps */
		static constexpr std::uint32_t RetryDelayUs = 10000;
		/** @brief Consecutive failed output calls after which the channel is reserved again */
		static constexpr std::int32_t FailuresBeforeNewChannel = 10;
		/** @brief Frames between two reports of failed output calls in the log */
		static constexpr std::int32_t FramesBetweenFailureReports = 60;

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
			std::int64_t Cursor = 0;
			std::uint32_t Queue[MaxQueuedBuffers] = {};
			std::int32_t QueueCount = 0;
			std::uint32_t Processed[MaxQueuedBuffers] = {};
			std::int32_t ProcessedCount = 0;
		};

		bool _valid;
		std::atomic<bool> _suspended;
		std::atomic<bool> _threadShouldQuit;
		/** @brief Hardware channel, only touched by the mixer thread while it runs; negative while none is reserved */
		std::int32_t _channel;
		/** @brief Failed output calls and channel reservations since the main thread last reported them */
		std::atomic<std::int32_t> _outputFailures;
		std::atomic<std::int32_t> _lastOutputError;
		std::atomic<std::int32_t> _channelReservations;
		std::int32_t _thread;
		/** @brief Kernel semaphore (count 1) guarding the sources, the buffers and the mix */
		std::int32_t _lock;
		/** @brief Two hardware blocks: one plays while the other is mixed */
		std::int16_t* _blocks;
		std::int32_t* _mixBuffer;
		/** @brief Rate the sources are mixed at; always divides @ref OutputFrequency (44100, 22050 or 11025) */
		std::int32_t _mixFrequency;
		/** @brief Last mixed frame of the previous block, where the next block's interpolation starts */
		std::int32_t _lastMixedLeft;
		std::int32_t _lastMixedRight;
		Buffer* _buffers;
		std::int32_t _bufferCount;
		std::int32_t _bufferCapacity;
		/** @brief Failed output calls not in the log yet, and frames since the last report - main thread only */
		std::int32_t _unreportedFailures = 0;
		std::int32_t _framesSinceFailureReport = FramesBetweenFailureReports;
		Source _sources[MaxSources];

		PspAudioDevice(const PspAudioDevice&) = delete;
		PspAudioDevice& operator=(const PspAudioDevice&) = delete;

		static int OutputThread(unsigned int args, void* argp);
		/** @brief Releases the channel, if any, and reserves a new one - on the mixer thread only */
		bool ReserveChannel();

		Source* GetSource(std::uint32_t sourceId);
		Buffer* GetActiveBuffer(Source& source);
		void ReleaseBuffer(Buffer& buffer);
		void MixInto(std::int16_t* output, std::int32_t frames);
		bool MixSource(Source& source, std::int32_t* output, std::int32_t frames);
		void ComputePanning(const Source& source, float& leftGain, float& rightGain) const;
		static void ReadFrame(const Buffer& buffer, std::int32_t frame, std::int32_t& left, std::int32_t& right);
	};
}

#endif
