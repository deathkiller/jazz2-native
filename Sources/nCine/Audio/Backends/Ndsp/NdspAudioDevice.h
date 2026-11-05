#pragma once

#if defined(WITH_NDSP) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

#include <atomic>

// libctru's individual headers carry no extern "C" of their own (only the umbrella <3ds.h> does)
extern "C" {
#include <3ds/types.h>
#include <3ds/synchronization.h>
#include <3ds/thread.h>
#include <3ds/ndsp/ndsp.h>
}

namespace nCine
{
	/**
		@brief Implementation of @ref IAudioDevice on top of the Nintendo 3DS's NDSP

		The PSP backend's design: sources are mixed in software into one NDSP channel fed by a ring of three
		wave buffers, on a thread above the game's priority so a long frame causes no dropout. Shared state is
		guarded by a `LightLock`, because spinning would never let the main thread release it. The DSP resamples
		the channel from the mixing rate (see @ref SetMixingFrequency()), so any rate works.

		NDSP needs the DSP firmware `sdmc:/3ds/dspfirm.cdc`, which cannot be shipped; without it the game runs
		silent (Azahar accepts any file by that name).
	*/
	class NdspAudioDevice : public AudioDeviceBase
	{
	public:
		NdspAudioDevice();
		~NdspAudioDevice() override;

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
		/** @brief The NDSP channel the mix plays through (any of the 24 would do) */
		static constexpr std::int32_t Channel = 0;
		/** @brief Rate the sources are mixed at unless @ref SetMixingFrequency() says otherwise (see the class documentation) */
		static constexpr std::int32_t DefaultMixingFrequency = 22050;
		/**
			@brief Frames per wave buffer - 23 ms at the default rate

			Also the latency of a newly started sound; three buffers keep the DSP fed while one is being filled.
		*/
		static constexpr std::int32_t BlockFrames = 512;
		static constexpr std::int32_t BlockCount = 3;
		static constexpr std::int32_t InitialBufferCapacity = 64;

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
		bool _ndspInitialized;
		std::atomic<bool> _suspended;
		std::atomic<bool> _threadShouldQuit;
		::Thread _thread;
		/** @brief Guards the sources, the buffers and the mix */
		LightLock _lock;
		/** @brief Signalled by the DSP's frame callback, so the mixer wakes as soon as a block is done */
		LightEvent _blockDone;
		/** @brief The wave buffers' sample data, in the linear heap the DSP reads by DMA */
		std::int16_t* _blocks;
		ndspWaveBuf _waveBufs[BlockCount];
		std::int32_t* _mixBuffer;
		/** @brief Rate the sources are mixed at; the DSP resamples the channel from it */
		std::int32_t _mixFrequency;
		Buffer* _buffers;
		std::int32_t _bufferCount;
		std::int32_t _bufferCapacity;
		Source _sources[MaxSources];

		NdspAudioDevice(const NdspAudioDevice&) = delete;
		NdspAudioDevice& operator=(const NdspAudioDevice&) = delete;

		static void OutputThread(void* arg);
		static void FrameCallback(void* arg);

		Source* GetSource(std::uint32_t sourceId);
		Buffer* GetActiveBuffer(Source& source);
		void ReleaseBuffer(Buffer& buffer);
		void FillBlock(std::int32_t index);
		void MixInto(std::int16_t* output, std::int32_t frames);
		bool MixSource(Source& source, std::int32_t* output, std::int32_t frames);
		void ComputePanning(const Source& source, float& leftGain, float& rightGain) const;
		static void ReadFrame(const Buffer& buffer, std::int32_t frame, std::int32_t& left, std::int32_t& right);
	};
}

#endif
