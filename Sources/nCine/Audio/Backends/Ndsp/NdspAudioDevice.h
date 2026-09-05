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

		The console's DSP mixes 24 hardware channels with per-channel resampling, volume and a filter stage,
		which is more than this game ever asks for - but a sound effect here is a whole sample handed to a
		channel, and the streaming queue every channel keeps holds a few buffers at most, so the OpenAL-shaped
		contract (sources, queued buffers, a sample-offset query) would have to be emulated on top of the
		channel API anyway. So this is the PSP backend's design, which is the SDL backend's mixer (which is the
		Amiga one, which is the N64 one): the sources are mixed by hand into one 16-bit stereo stream with a
		32-bit integer accumulator, 32.32 fixed-point cursors and Q15 gains, and the mix goes out through ONE
		NDSP channel as a ring of three wave buffers. Where it runs is the same departure the PSP made from the
		main-thread mixers: a thread of higher priority than the game's mixes each block right after the DSP
		finishes one and hands it back, so a frame that runs long on this console - a busy scene, a level
		load - costs nothing audible. The price is a lock: the mixer reads source and buffer state the game
		writes from the main thread, so every operation on them takes a `LightLock`, held for microseconds by
		the game and for the length of one block's mix by the mixer. A blocking lock rather than a spin lock,
		because the mixer thread has the higher priority - spinning on a lock the main thread holds would never
		let it be released.

		What the DSP does contribute is the resampling: the channel is told the rate the sources are mixed at -
		22050 Hz by default, the user's "Sample Rate" option otherwise (see @ref setMixingFrequency()) - and
		the hardware brings it to its own 32728 Hz with linear interpolation, so unlike on the PSP no upsampling
		pass is needed and any rate is a valid choice. @ref nativeFrequency() reports the mixing rate, so the
		module decoder renders at it too.

		NDSP needs the DSP's firmware, which libctru loads from `sdmc:/3ds/dspfirm.cdc` - a file every console
		running homebrew has (DSP1 dumps it) but which cannot be shipped. Without it the device stays silent and
		the game runs on regardless; Azahar, whose DSP is emulated in software, accepts any file by that name.
	*/
	class NdspAudioDevice : public AudioDeviceBase
	{
	public:
		NdspAudioDevice();
		~NdspAudioDevice() override;

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
		static constexpr std::int32_t MaxSources = 32;
		static constexpr std::int32_t MaxQueuedBuffers = 4;
		static constexpr std::int32_t ChannelCount = 2;
		/** @brief The NDSP channel the mix plays through (any of the 24 would do) */
		static constexpr std::int32_t Channel = 0;
		/** @brief Rate the sources are mixed at unless @ref setMixingFrequency() says otherwise (see the class documentation) */
		static constexpr std::int32_t DefaultMixingFrequency = 22050;
		/**
			@brief Frames per wave buffer - 23 ms at the default rate

			Also the latency: a sound started by the game is in the block mixed next, which plays after the
			ones already queued. Three buffers keep the DSP fed across the one the mixer is filling.
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
