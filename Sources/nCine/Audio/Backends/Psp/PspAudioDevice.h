#pragma once

#if defined(WITH_PSPAUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

#include <atomic>

namespace nCine
{
	/**
		@brief Implementation of @ref IAudioDevice on top of the PSP's `sceAudio` hardware channel

		pspdev does ship an OpenAL, but it is an OpenAL Soft 1.6 from 2008 whose mixer filters and resamples
		every source in floating point under one global lock, and on this console's single 333 MHz core its
		thread alone measured 12-24% of the CPU in game (per-thread run clocks, `prince/03_carrot1`) - as much
		as the whole module decoder. The game does not need any of what that buys: no effects, no HRTF, no
		filters, and its sounds are 8- and 16-bit samples at 11-22 kHz.

		So this is the SDL backend's mixer (which is the Amiga one, which is the N64 one): sources are mixed
		by hand into one 16-bit stereo stream with a 32-bit integer accumulator, 32.32 fixed-point cursors and
		Q15 gains. Where it runs is the one departure from those backends, which mix on the main thread once
		per rendered frame: here a thread of higher priority than the game's mixes each block right before it
		hands it to `sceAudioOutputPannedBlocking()`, double-buffered, and the hardware paces it at the sample
		rate. A frame that runs long on this console - a busy scene, a level load - then costs nothing audible,
		where a main-thread mixer with a few blocks of lead went silent the moment a frame outlasted them
		(measured: dropouts in the same scenes that dip below 30 fps). The price is a lock: the mixer reads
		source and buffer state the game writes from the main thread, so every operation on them takes a
		kernel semaphore, held for microseconds by the game and for the length of one block's mix (well under
		a millisecond) by the mixer. A kernel semaphore rather than a spin lock, because the mixer thread has
		the higher priority - spinning on a lock the main thread holds would never let it be released.

		The hardware plays 44100 Hz and nothing else, but the sources are mixed at a lower rate - 22050 Hz by
		default, the user's "Sample Rate" option otherwise (see @ref setMixingFrequency()) - and the block is
		upsampled to the hardware's rate afterwards with a linear interpolation. The per-source loop, which is
		where the mixer's time goes, then runs half or a quarter as often, for content that is 11-22 kHz
		samples to begin with; the interpolation is one multiply-add per output sample, whatever the number of
		sources. @ref nativeFrequency() reports the mixing rate, so the module decoder renders at it too.
	*/
	class PspAudioDevice : public AudioDeviceBase
	{
	public:
		PspAudioDevice();
		~PspAudioDevice() override;

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
		/** @brief The hardware's native rate; `sceAudioChReserve()` channels play nothing else */
		static constexpr std::int32_t OutputFrequency = 44100;
		/** @brief Rate the sources are mixed at unless @ref setMixingFrequency() says otherwise (see the class documentation) */
		static constexpr std::int32_t DefaultMixingFrequency = 22050;
		/**
			@brief Frames per hardware block (a multiple of 64, as `sceAudioChReserve()` requires) - 23 ms

			Also the latency: a sound started by the game is in the block mixed next, which plays after the
			one the hardware is on.
		*/
		static constexpr std::int32_t BlockFrames = 1024;
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
		std::atomic<bool> _suspended;
		std::atomic<bool> _threadShouldQuit;
		std::int32_t _channel;
		std::int32_t _thread;
		/** @brief Kernel semaphore (count 1) guarding the sources, the buffers and the mix */
		std::int32_t _lock;
		/** @brief Two hardware blocks: one plays while the other is mixed */
		std::int16_t* _blocks;
		std::int32_t* _mixBuffer;
		/** @brief Rate the sources are mixed at; always divides @ref OutputFrequency (44100, 22050 or 11025) */
		std::int32_t _mixFrequency;
		/** @brief Last mixed frame of the previous block, the interpolation of the next block's first frames starts from it */
		std::int32_t _lastMixedLeft;
		std::int32_t _lastMixedRight;
		Buffer* _buffers;
		std::int32_t _bufferCount;
		std::int32_t _bufferCapacity;
		Source _sources[MaxSources];

		PspAudioDevice(const PspAudioDevice&) = delete;
		PspAudioDevice& operator=(const PspAudioDevice&) = delete;

		static int OutputThread(unsigned int args, void* argp);

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
