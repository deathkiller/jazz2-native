#pragma once

#if defined(WITH_PS3AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../AudioDeviceBase.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief PlayStation 3 implementation of @ref IAudioDevice on top of PSL1GHT's libaudio

		lv2 only provides a ring of 256-sample blocks of interleaved float samples, so mixing, linear
		resampling to 48 kHz, panning and stream queues are all done here on the PPE, and source setters
		like @ref SetSourceGain() only record values that @ref MixInto() applies. PSL1GHT has no pthreads,
		so the ring is filled from @ref UpdatePlayers() once per frame and is long enough (@ref BlockCount)
		to survive a late frame.
	*/
	class Ps3AudioDevice : public AudioDeviceBase
	{
	public:
		Ps3AudioDevice();
		~Ps3AudioDevice() override;

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
		/** @brief Sources the mixer walks; a silent one costs nothing, so this is generous */
		static constexpr std::int32_t MaxSources = 32;
		/** @brief Upper bound on the streaming queue of a source (@ref AudioStream uses three) */
		static constexpr std::int32_t MaxQueuedBuffers = 4;
		/**
			@brief Blocks the libaudio ring holds

			The port accepts 8, 16 or 32; 16 blocks (about 85 ms) let the ring survive a late frame.
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

				A double, because a float loses sub-sample precision after a few minutes of one buffer.
			*/
			double Cursor = 0.0;

			/** @brief Buffers queued on a streaming source, oldest first */
			std::uint32_t Queue[MaxQueuedBuffers] = {};
			std::int32_t QueueCount = 0;
			/** @brief Buffers played to the end, waiting to be collected by @ref UnqueueBuffers() */
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

		/** @brief Returns the source of an id handed out by @ref RegisterPlayer(), or `nullptr` */
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
