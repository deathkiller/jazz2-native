#pragma once

#if defined(WITH_PS3AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief PlayStation 3 implementation of @ref IAudioDevice on top of PSL1GHT's libaudio

		The console has no mixer to talk to. Where the Dreamcast's AICA is a 64-voice wavetable part with
		its own sound RAM and the Wii's DSP runs libogc's ASND, lv2 hands a title exactly one thing: a ring
		of 256-sample blocks of interleaved 32-bit float samples, which the audio hardware scans out
		verbatim. Everything above that - mixing, resampling, panning, looping, stream queues - is this
		class, on the PPE.

		**Two consequences shape the whole design.**

		*Everything is mixed by hand into one output.* There is no per-source volume or pitch register to
		program, so @ref setSourceGain() and friends only record a value that @ref MixInto() applies while
		it walks the sources. That also means a source costs CPU only while it is audible, which is why
		@ref MaxSources can be generous.

		*There is no mixer thread.* PSL1GHT ships no pthreads (see the `NCINE_WITH_THREADS` arm in
		`ncine_options.cmake`), so the ring cannot be topped up from a callback the way every other backend
		does it - it is filled from @ref updatePlayers(), once per frame, on the main thread. That works
		because the ring is deliberately long: @ref BlockCount blocks of 256 samples at 48 kHz is about
		85 ms of audio, so a frame may take five times its 16.6 ms budget before the ring runs dry. What it
		costs is latency, which for this game is not a meaningful trade.

		Resampling is linear interpolation from the buffer's own rate to the port's 48 kHz. The PPE has the
		headroom for it at these voice counts, and the alternative - resampling every asset offline to one
		rate - would trade quality for memory the console does not need to save.
	*/
	class Ps3AudioDevice : public AudioDeviceBase
	{
	public:
		Ps3AudioDevice();
		~Ps3AudioDevice() override;

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
		/**
			@brief Blocks the libaudio ring holds

			The port accepts 8, 16 or 32. Sixteen is chosen for the reason given in the class
			documentation: with no mixer thread the ring has to survive a late frame, and 16 blocks of 256
			samples at 48 kHz is roughly 85 ms of slack.
		*/
		static constexpr std::uint32_t BlockCount = 16;
		/** @brief Samples in one block, fixed by the hardware */
		static constexpr std::uint32_t BlockSamples = 256;
		/** @brief Channels the port is opened with (the game's own mix is stereo) */
		static constexpr std::uint32_t ChannelCount = 2;
		/** @brief Rate the audio hardware scans out at; every source is resampled to it */
		static constexpr std::int32_t OutputFrequency = 48000;

		/** @brief One uploaded PCM buffer, kept as interleaved 16-bit samples in main memory */
		struct Buffer
		{
			bool Used = false;
			SmallVector<std::int16_t, 0> Samples;
			std::int32_t ChannelCount = 1;
			std::int32_t Frequency = OutputFrequency;
			/** @brief Frames (sample pairs for stereo), which is what the mixer's cursor counts in */
			std::int32_t FrameCount = 0;
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
				@brief Playback cursor in frames, fractional because of the resampling

				Kept as a double rather than a float: at 48 kHz a float's 24-bit mantissa starts losing
				sub-sample precision after about six minutes of a single buffer, which a long music stream
				would reach.
			*/
			double Cursor = 0.0;

			/** @brief Buffers queued on a streaming source, oldest first */
			std::uint32_t Queue[MaxQueuedBuffers] = {};
			std::int32_t QueueCount = 0;
			/** @brief Buffers played to the end, waiting to be collected by @ref unqueueBuffers() */
			std::uint32_t Processed[MaxQueuedBuffers] = {};
			std::int32_t ProcessedCount = 0;
		};

		bool _valid;
		bool _suspended;
		std::uint32_t _portNumber;
		/** @brief Base of the ring the hardware scans out, and the read cursor it publishes */
		float* _portBuffer;
		std::uint64_t* _readIndexAddress;
		/** @brief Next block this side will write, chasing the hardware's read index */
		std::uint32_t _writeBlock;

		SmallVector<Buffer, 0> _buffers;
		Source _sources[MaxSources];

		/** @brief Scratch the frame's audio is accumulated into before it is written to the ring */
		SmallVector<float, 0> _mixBuffer;

		/** @brief Returns the source of an id handed out by @ref registerPlayer(), or `nullptr` */
		Source* GetSource(std::uint32_t sourceId);

		/** @brief Fills every ring block the hardware has already played past */
		void FillRing();
		/** @brief Mixes @p frames of every playing source into @ref _mixBuffer (which it clears first) */
		void MixInto(float* output, std::uint32_t frames);
		/**
			@brief Mixes one source, advancing its cursor and retiring its buffers

			@returns `false` once the source has run out of audio, which stops it
		*/
		bool MixSource(Source& source, float* output, std::uint32_t frames);
		/** @brief Returns the buffer a source is currently reading from, or `nullptr` */
		Buffer* GetActiveBuffer(Source& source);
		/** @brief Computes the left/right gains of a source from its position and the listener's */
		void ComputePanning(const Source& source, float& leftGain, float& rightGain) const;
	};
}

#endif
