#pragma once

#include "../../Main.h"
#if !defined(NCINE_HAS_NATIVE_AUDIO)
#	include "IAudioReader.h"
#endif
#include "../Primitives/Vector3.h"
#include "../Base/FrameTimer.h"

#include <Containers/StringView.h>

#include <atomic>
#include <memory>

namespace nCine
{
	class IAudioPlayer;

#if !defined(NCINE_HAS_NATIVE_AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)
	/**
		@brief Request to decode one buffer of audio stream data, usually on the decoding thread

		Shared by the @ref AudioStream and the device, so it outlives a stream destroyed mid-decode.
		One request is reused for the whole lifetime of a stream.
	*/
	struct StreamDecodeRequest
	{
		/** @brief State of the request */
		enum class State : std::uint8_t {
			Idle,		/**< Not submitted, the buffer contents are not valid */
			Pending,	/**< Submitted, the request is owned by the decoding thread */
			Ready		/**< Executed, the buffer holds @ref BytesRead decoded bytes */
		};

		/** @brief Reader that decodes the data, shared with the owning stream */
		std::shared_ptr<IAudioReader> Reader;
		/** @brief Destination buffer for the decoded data */
		std::unique_ptr<char[]> Buffer;
		/** @brief Size of the destination buffer in bytes */
		std::int32_t BufferSize;
		/** @brief Number of decoded bytes, valid when the state is @ref State::Ready */
		std::int32_t BytesRead;
		/** @brief Whether the reader rewinds and keeps decoding when the end of data is reached */
		bool Looping;
		/** @brief Current state of the request */
		std::atomic<State> CurrentState;

		StreamDecodeRequest()
			: BufferSize(0), BytesRead(0), Looping(false), CurrentState(State::Idle) {}

		/** @brief Decodes one buffer of data and marks the request as ready */
		void Execute()
		{
			std::int32_t bytes = Reader->Read(Buffer.get(), BufferSize);
			if (bytes < BufferSize && Looping) {
				// EOF reached, rewind and fill the rest of the buffer from the beginning
				Reader->Rewind();
				bytes += Reader->Read(Buffer.get() + bytes, BufferSize - bytes);
			}
			BytesRead = bytes;
			CurrentState.store(State::Ready, std::memory_order_release);
		}

		/**
			@brief Decodes at most @p maxBytes more of the buffer, continuing where the previous call stopped

			For devices without a decoding thread. @ref BytesRead must be zero when the request is submitted.
			@return `true` once the buffer is full or the data has ended, which is when the request is marked ready
		*/
		bool ExecuteSlice(std::int32_t maxBytes)
		{
			const std::int32_t wanted = (maxBytes < BufferSize - BytesRead ? maxBytes : BufferSize - BytesRead);
			std::int32_t bytes = Reader->Read(Buffer.get() + BytesRead, wanted);
			if (bytes < wanted && Looping) {
				Reader->Rewind();
				bytes += Reader->Read(Buffer.get() + BytesRead + bytes, wanted - bytes);
			}
			BytesRead += bytes;
			if (BytesRead < BufferSize && bytes == wanted) {
				return false;
			}
			CurrentState.store(State::Ready, std::memory_order_release);
			return true;
		}
	};
#endif

	/**
		@brief Interface for an audio device backend

		Owns the listener, sources, buffers and active players. Exactly one backend from `nCine/Audio/Backends/`
		is compiled in, players refer to its buffers and sources only by opaque ids.

		Where `NCINE_HAS_NATIVE_AUDIO` is defined, the device plays files by itself and nothing is decoded.
	*/
	class IAudioDevice
	{
	public:
#if !defined(NCINE_HAS_NATIVE_AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Sample format of an audio buffer */
		enum class BufferFormat {
			Mono8,		/**< 8-bit unsigned, single channel */
			Stereo8,	/**< 8-bit unsigned, two channels */
			Mono16,		/**< 16-bit signed, single channel */
			Stereo16	/**< 16-bit signed, two channels */
		};
#endif

		/**
		 * @brief What a buffer is going to be used for
		 *
		 * Most backends ignore it, it matters where sample memory isn't uniform (e.g. Dreamcast sound RAM).
		 */
		enum class BufferUsage {
			Static,		/**< Loaded once and played as a whole, by @ref AudioBuffer */
			Streaming	/**< Refilled continuously and played through a queue, by @ref AudioStream */
		};

		/** @{ @name Constants */

		/** @brief Value returned by @ref RegisterPlayer() when no source is available */
		static constexpr std::uint32_t UnavailableSource = ~0u;

		/** @brief Scale factor converting game length units to physical (OpenAL) units */
		static constexpr float LengthToPhysical = 1.0f / 100.0f;
		/** @brief Scale factor converting game velocity units to physical (OpenAL) units */
		static constexpr float VelocityToPhysical = FrameTimer::FramesPerSecond / 100.0f;
		/** @brief Distance at which attenuation begins, in physical units */
		static constexpr float ReferenceDistance = 200.0f * LengthToPhysical;
		/** @brief Distance beyond which attenuation no longer increases, in physical units */
		static constexpr float MaxDistance = 900.0f * LengthToPhysical;

		/** @} */

		/** @brief Player backing type */
		enum class PlayerType {
			Buffer,	/**< Player backed by a fully loaded @ref AudioBuffer */
			Stream	/**< Player decoding an @ref AudioStream on the fly */
		};

		/** @brief Returns `true` if the device was initialized successfully */
		virtual bool IsValid() const = 0;

		/** @brief Returns the name of the underlying device */
		virtual const char* GetName() const = 0;

		virtual ~IAudioDevice() = 0;

		/** @brief Returns the listener gain (master volume) */
		virtual float GetGain() const = 0;
		/** @brief Sets the listener gain (master volume) */
		virtual void SetGain(float gain) = 0;

		/** @brief Returns the maximum number of players that can be active at once */
		virtual std::uint32_t GetMaxPlayerCount() const = 0;
		/** @brief Returns the number of currently active players */
		virtual std::uint32_t GetPlayerCount() const = 0;
		/** @brief Returns the active player at the specified index */
		virtual const IAudioPlayer* GetPlayer(std::uint32_t index) const = 0;
		/** @overload */
		virtual IAudioPlayer* GetPlayer(std::uint32_t index) = 0;

		/** @brief Stops every player currently playing */
		virtual void StopPlayers() = 0;
		/** @brief Pauses every player currently playing */
		virtual void PausePlayers() = 0;
		/** @brief Stops every player of the specified type */
		virtual void StopPlayers(PlayerType playerType) = 0;
		/** @brief Pauses every player of the specified type */
		virtual void PausePlayers(PlayerType playerType) = 0;

		/** @brief Pauses every player currently playing while keeping it registered */
		virtual void FreezePlayers() = 0;
		/** @brief Resumes every player previously paused by @ref FreezePlayers() */
		virtual void UnfreezePlayers() = 0;

		/** @brief Registers a player so it receives state and buffer queue updates, returning its source id */
		virtual std::uint32_t RegisterPlayer(IAudioPlayer* player) = 0;
		/** @brief Unregisters a previously registered player */
		virtual void UnregisterPlayer(IAudioPlayer* player) = 0;
		/** @brief Updates the state of every registered player, including the buffer queue of stream players */
		virtual void UpdatePlayers() = 0;

#if !defined(NCINE_HAS_NATIVE_AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
		 * @brief Submits a decode request to be executed asynchronously on the decoding thread
		 *
		 * The request state must be @ref StreamDecodeRequest::State::Pending when submitted.
		 * @return `false` when no decoding thread is available and the caller has to decode synchronously
		 */
		virtual bool SubmitStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request) = 0;
		/**
		 * @brief Ensures the specified request is neither queued nor being executed when this method returns
		 *
		 * Required before touching the request's reader. A queued request is reset to
		 * @ref StreamDecodeRequest::State::Idle, a running one is waited for.
		 */
		virtual void DrainStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request) = 0;
#endif

		/**
		 * @brief Does one short step of background work in time the caller would otherwise spend waiting
		 *
		 * Called while waiting for vertical blank, for devices that decode streams without a thread.
		 * @param availableMs   Time left, a step that would not fit is not started
		 * @return `true` if a step was done and more remains
		 */
		virtual bool RunIdleWork(float availableMs) { static_cast<void>(availableMs); return false; }

		/** @brief Returns the 3D position of the listener */
		virtual const Vector3f& GetListenerPosition() const = 0;
		/** @brief Updates the position and velocity of the listener */
		virtual void UpdateListener(const Vector3f& position, const Vector3f& velocity) = 0;

		/** @brief Returns the native sample rate of the device */
		virtual std::int32_t GetNativeFrequency() = 0;
		/**
		 * @brief Changes the rate the device mixes at, if the backend has one to change
		 *
		 * Honoured only by software mixers (PSP, Amiga), @ref GetNativeFrequency() then reports the new rate.
		 * An unsupported rate or `0` is ignored, a no-op on other backends.
		 */
		virtual void SetMixingFrequency(std::int32_t frequency) {
			static_cast<void>(frequency);
		}

		/** @{ @name Buffers */

		/** @brief Creates an empty backend buffer, returning its id or `0` on failure */
		virtual std::uint32_t CreateBuffer(BufferUsage usage) = 0;
		/** @brief Destroys a buffer previously returned by @ref CreateBuffer() */
		virtual void DeleteBuffer(std::uint32_t bufferId) = 0;
#if !defined(NCINE_HAS_NATIVE_AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Replaces the contents of a buffer with the specified samples */
		virtual bool UploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency) = 0;
#endif

		/** @} */

#if defined(NCINE_HAS_NATIVE_AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @{ @name Native playback */

		/**
		 * @brief Properties of audio the device plays in a format of its own
		 *
		 * Filled in by @ref LoadNativeBuffer() and @ref OpenNativeStream().
		 */
		struct NativeAudioInfo
		{
			/** @brief Bytes per decoded sample, or `0` where the content is not a waveform (a tracker module) */
			std::int32_t BytesPerSample = 0;
			/** @brief Number of channels */
			std::int32_t NumChannels = 0;
			/** @brief Sample frequency, or `0` if not known */
			std::int32_t Frequency = 0;
			/** @brief Total number of samples, or `-1` if not known */
			std::int32_t NumSamples = -1;
		};

		/**
		 * @brief Makes a buffer play a file the device reads by itself instead of holding its decoded samples
		 *
		 * Used e.g. on Nintendo 64 to stream sounds from the cartridge. Returns `false` if the file can't be played.
		 */
		virtual bool LoadNativeBuffer(std::uint32_t bufferId, Death::Containers::StringView path, NativeAudioInfo& info) = 0;
		/** @brief Opens a file the device streams by itself, returning its id or `0` if it can't be played */
		virtual std::uint32_t OpenNativeStream(Death::Containers::StringView path, NativeAudioInfo& info) = 0;
		/** @brief Closes a stream returned by @ref OpenNativeStream(), stopping it if it is playing */
		virtual void CloseNativeStream(std::uint32_t streamId) = 0;
		/**
		 * @brief Binds a native stream to a source
		 *
		 * The source controls then drive the stream (@ref StopSource() also rewinds it).
		 * The binding lasts until the source is handed out again.
		 */
		virtual void SetSourceNativeStream(std::uint32_t sourceId, std::uint32_t streamId) = 0;

		/** @} */
#endif

		/** @{ @name Sources */

		/** @brief Attaches a buffer to a source for non-streamed playback, `0` detaches the current one */
		virtual void SetSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId) = 0;
		/** @brief Sets the gain of a source */
		virtual void SetSourceGain(std::uint32_t sourceId, float gain) = 0;
		/** @brief Sets the pitch of a source, as a multiplier of its natural playback rate */
		virtual void SetSourcePitch(std::uint32_t sourceId, float pitch) = 0;
		/** @brief Sets whether a source repeats its attached buffer */
		virtual void SetSourceLooping(std::uint32_t sourceId, bool looping) = 0;
		/** @brief Sets whether the position of a source is relative to the listener */
		virtual void SetSourceRelative(std::uint32_t sourceId, bool relative) = 0;
		/** @brief Sets the position of a source, in physical units */
		virtual void SetSourcePosition(std::uint32_t sourceId, const Vector3f& position) = 0;
		/** @brief Sets the low-pass amount of a source, `1.0f` disables the filter */
		virtual void SetSourceLowPass(std::uint32_t sourceId, float value) = 0;

		/** @brief Returns the playback position of a source in samples */
		virtual std::int32_t GetSourceSampleOffset(std::uint32_t sourceId) = 0;
		/** @brief Sets the playback position of a source in samples */
		virtual void SetSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset) = 0;

		/** @brief Starts or resumes a source */
		virtual void PlaySource(std::uint32_t sourceId) = 0;
		/** @brief Pauses a source at its current position */
		virtual void PauseSource(std::uint32_t sourceId) = 0;
		/** @brief Stops a source */
		virtual void StopSource(std::uint32_t sourceId) = 0;
		/** @brief Returns `true` if a source is still producing sound */
		virtual bool IsSourcePlaying(std::uint32_t sourceId) = 0;

		/** @} */

#if !defined(NCINE_HAS_NATIVE_AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @{ @name Streaming */

		/** @brief Appends a buffer to the streaming queue of a source */
		virtual void QueueBuffer(std::uint32_t sourceId, std::uint32_t bufferId) = 0;
		/** @brief Returns the number of queued buffers a source has finished playing */
		virtual std::int32_t GetProcessedBufferCount(std::uint32_t sourceId) = 0;
		/**
		 * @brief Removes the specified number of played buffers from the front of the queue
		 *
		 * @param bufferIds Receives the ids of the removed buffers, must hold @p count entries
		 */
		virtual void UnqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds) = 0;

		/** @} */
#endif

#if defined(WITH_LIBRETRO)
		/**
		 * @brief Renders the next block of mixed audio into the caller's buffer (16-bit interleaved stereo)
		 *
		 * @return `false` when the device cannot render on demand and the caller has to supply silence
		 */
		virtual bool RenderSamples(std::int16_t* buffer, std::int32_t numFrames) = 0;
#endif

		/**
		 * @brief Tells the device the caller is about to stop feeding it for a long time
		 *
		 * For devices mixing on the main thread, which would starve during e.g. a level load (PS2 `audsrv`
		 * repeats its last buffer). No-op for devices with their own mixing thread.
		 */
		virtual void BeginBlockingOperation() { }
		/** @brief Ends what @ref BeginBlockingOperation() started, and resumes playback */
		virtual void EndBlockingOperation() { }

		/** @brief Suspends the audio device */
		virtual void SuspendDevice() = 0;
		/** @brief Resumes the audio device */
		virtual void ResumeDevice() = 0;
	};

	inline IAudioDevice::~IAudioDevice() { }

#ifndef DOXYGEN_GENERATING_OUTPUT
	// Silent fallback device used when no audio backend is available
	class NullAudioDevice : public IAudioDevice
	{
	public:
		bool IsValid() const override {
			return false;
		}

		const char* GetName() const override {
			return "NullAudioDevice";
		}

		float GetGain() const override {
			return 1.0f;
		}
		void SetGain(float gain) override { }

		std::uint32_t GetMaxPlayerCount() const override {
			return 0;
		}
		std::uint32_t GetPlayerCount() const override {
			return 0;
		}
		const IAudioPlayer* GetPlayer(std::uint32_t index) const override {
			return nullptr;
		}
		IAudioPlayer* GetPlayer(std::uint32_t index) override {
			return nullptr;
		}

		void StopPlayers() override { }
		void PausePlayers() override { }
		void StopPlayers(PlayerType playerType) override { }
		void PausePlayers(PlayerType playerType) override { }

		void FreezePlayers() override { }
		void UnfreezePlayers() override { }

		std::uint32_t RegisterPlayer(IAudioPlayer* player) override { return UnavailableSource; }
		void UnregisterPlayer(IAudioPlayer* player) override { }
		void UpdatePlayers() override {}
#if !defined(NCINE_HAS_NATIVE_AUDIO)
		bool SubmitStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request) override { return false; }
		void DrainStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request) override { }
#endif
		const Vector3f& GetListenerPosition() const override { return Vector3f::Zero; }
		void UpdateListener(const Vector3f& position, const Vector3f& velocity) override { }
		std::int32_t GetNativeFrequency() override { return 0; }

		std::uint32_t CreateBuffer(BufferUsage usage) override { return 0; }
		void DeleteBuffer(std::uint32_t bufferId) override { }
#if defined(NCINE_HAS_NATIVE_AUDIO)
		bool LoadNativeBuffer(std::uint32_t bufferId, Death::Containers::StringView path, NativeAudioInfo& info) override { return false; }
		std::uint32_t OpenNativeStream(Death::Containers::StringView path, NativeAudioInfo& info) override { return 0; }
		void CloseNativeStream(std::uint32_t streamId) override { }
		void SetSourceNativeStream(std::uint32_t sourceId, std::uint32_t streamId) override { }
#else
		bool UploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency) override { return false; }
#endif

		void SetSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId) override { }
		void SetSourceGain(std::uint32_t sourceId, float gain) override { }
		void SetSourcePitch(std::uint32_t sourceId, float pitch) override { }
		void SetSourceLooping(std::uint32_t sourceId, bool looping) override { }
		void SetSourceRelative(std::uint32_t sourceId, bool relative) override { }
		void SetSourcePosition(std::uint32_t sourceId, const Vector3f& position) override { }
		void SetSourceLowPass(std::uint32_t sourceId, float value) override { }
		std::int32_t GetSourceSampleOffset(std::uint32_t sourceId) override { return 0; }
		void SetSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset) override { }
		void PlaySource(std::uint32_t sourceId) override { }
		void PauseSource(std::uint32_t sourceId) override { }
		void StopSource(std::uint32_t sourceId) override { }
		bool IsSourcePlaying(std::uint32_t sourceId) override { return false; }

#if !defined(NCINE_HAS_NATIVE_AUDIO)
		void QueueBuffer(std::uint32_t sourceId, std::uint32_t bufferId) override { }
		std::int32_t GetProcessedBufferCount(std::uint32_t sourceId) override { return 0; }
		void UnqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds) override { }
#endif

#if defined(WITH_LIBRETRO)
		bool RenderSamples(std::int16_t* buffer, std::int32_t numFrames) override { return false; }
#endif

		void SuspendDevice() override { }
		void ResumeDevice() override { }
	};
#endif
}
