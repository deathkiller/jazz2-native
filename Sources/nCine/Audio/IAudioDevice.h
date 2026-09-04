#pragma once

#include "IAudioReader.h"
#include "../Primitives/Vector3.h"
#include "../Base/FrameTimer.h"

#include <atomic>
#include <memory>

namespace nCine
{
	class IAudioPlayer;

	/**
		@brief Request to decode one buffer of audio stream data, usually on the decoding thread

		Owned jointly by the requesting @ref AudioStream and, while submitted, by the audio device.
		The shared ownership keeps the reader and the destination buffer alive even if the stream
		is moved or destroyed while the request is still being executed. One request is reused for
		the whole lifetime of a stream and holds at most one chunk of decoded data.
	*/
	struct StreamDecodeRequest
	{
		/** @brief State of the request */
		enum class State : std::uint8_t {
			Idle,		/**< Not submitted, the buffer contents are not valid */
			Pending,	/**< Submitted, the request is owned by the decoding thread */
			Ready		/**< Executed, the buffer holds @ref bytesRead decoded bytes */
		};

		/** @brief Reader that decodes the data, shared with the owning stream */
		std::shared_ptr<IAudioReader> reader;
		/** @brief Destination buffer for the decoded data */
		std::unique_ptr<char[]> buffer;
		/** @brief Size of the destination buffer in bytes */
		std::int32_t bufferSize;
		/** @brief Number of decoded bytes, valid when the state is @ref State::Ready */
		std::int32_t bytesRead;
		/** @brief Whether the reader rewinds and keeps decoding when the end of data is reached */
		bool looping;
		/** @brief Current state of the request */
		std::atomic<State> state;

		StreamDecodeRequest()
			: bufferSize(0), bytesRead(0), looping(false), state(State::Idle) {}

		/** @brief Decodes one buffer of data and marks the request as ready */
		void Execute()
		{
			std::int32_t bytes = reader->read(buffer.get(), bufferSize);
			if (bytes < bufferSize && looping) {
				// EOF reached, rewind and fill the rest of the buffer from the beginning
				reader->rewind();
				bytes += reader->read(buffer.get() + bytes, bufferSize - bytes);
			}
			bytesRead = bytes;
			state.store(State::Ready, std::memory_order_release);
		}
	};

	/**
		@brief Interface for an audio device backend

		Manages the listener, the pool of audio sources and all active players, and owns the
		backend objects (buffers and sources) the shared player classes drive through it. Exactly
		one implementation is compiled into a binary, each living in `nCine/Audio/Backends/`:
		@ref ALAudioDevice on top of OpenAL, @ref AsndAudioDevice on top of the Wii/GameCube DSP
		mixer, @ref AicaAudioDevice on top of the Dreamcast sound processor, and
		@ref NullAudioDevice as a silent fallback.

		@ref AudioBuffer, @ref IAudioPlayer and @ref AudioStream contain no backend calls of
		their own - they refer to buffers and sources by the opaque ids handed out here.
	*/
	class IAudioDevice
	{
	public:
		/** @brief Sample format of an audio buffer */
		enum class BufferFormat {
			Mono8,		/**< 8-bit unsigned, single channel */
			Stereo8,	/**< 8-bit unsigned, two channels */
			Mono16,		/**< 16-bit signed, single channel */
			Stereo16	/**< 16-bit signed, two channels */
		};

		/**
		 * @brief What a buffer is going to be used for
		 *
		 * Most backends ignore this, but where sample memory is not one uniform pool the two kinds
		 * have to live in different places: the Dreamcast plays a fully loaded sound out of the
		 * AICA's own sound RAM, while a streamed one stays in main memory and is transferred into
		 * the sound processor's ring buffer a chunk at a time.
		 */
		enum class BufferUsage {
			Static,		/**< Loaded once and played as a whole, by @ref AudioBuffer */
			Streaming	/**< Refilled continuously and played through a queue, by @ref AudioStream */
		};

		/** @{ @name Constants */

		/** @brief Value returned by @ref registerPlayer() when no source is available */
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
		virtual bool isValid() const = 0;

		/** @brief Returns the name of the underlying device */
		virtual const char* name() const = 0;

		virtual ~IAudioDevice() = 0;

		/** @brief Returns the listener gain (master volume) */
		virtual float gain() const = 0;
		/** @brief Sets the listener gain (master volume) */
		virtual void setGain(float gain) = 0;

		/** @brief Returns the maximum number of players that can be active at once */
		virtual std::uint32_t maxNumPlayers() const = 0;
		/** @brief Returns the number of currently active players */
		virtual std::uint32_t numPlayers() const = 0;
		/** @brief Returns the active player at the specified index */
		virtual const IAudioPlayer* player(std::uint32_t index) const = 0;
		/** @overload */
		virtual IAudioPlayer* player(std::uint32_t index) = 0;

		/** @brief Stops every player currently playing */
		virtual void stopPlayers() = 0;
		/** @brief Pauses every player currently playing */
		virtual void pausePlayers() = 0;
		/** @brief Stops every player of the specified type */
		virtual void stopPlayers(PlayerType playerType) = 0;
		/** @brief Pauses every player of the specified type */
		virtual void pausePlayers(PlayerType playerType) = 0;

		/** @brief Pauses every player currently playing while keeping it registered */
		virtual void freezePlayers() = 0;
		/** @brief Resumes every player previously paused by @ref freezePlayers() */
		virtual void unfreezePlayers() = 0;

		/** @brief Registers a player so it receives state and buffer queue updates, returning its source id */
		virtual std::uint32_t registerPlayer(IAudioPlayer* player) = 0;
		/** @brief Unregisters a previously registered player */
		virtual void unregisterPlayer(IAudioPlayer* player) = 0;
		/** @brief Updates the state of every registered player, including the buffer queue of stream players */
		virtual void updatePlayers() = 0;

		/**
		 * @brief Submits a decode request to be executed asynchronously on the decoding thread
		 *
		 * The request state must be @ref StreamDecodeRequest::State::Pending when submitted.
		 * @return `false` when no decoding thread is available and the caller has to decode synchronously
		 */
		virtual bool submitStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request) = 0;
		/**
		 * @brief Ensures the specified request is neither queued nor being executed when this method returns
		 *
		 * Required before the caller touches the request's reader (rewinding, changing looping or
		 * replacing it). A request removed from the queue before execution is reset to
		 * @ref StreamDecodeRequest::State::Idle, a request already being executed is waited for.
		 */
		virtual void drainStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request) = 0;

		/** @brief Returns the 3D position of the listener */
		virtual const Vector3f& getListenerPosition() const = 0;
		/** @brief Updates the position and velocity of the listener */
		virtual void updateListener(const Vector3f& position, const Vector3f& velocity) = 0;

		/** @brief Returns the native sample rate of the device */
		virtual std::int32_t nativeFrequency() = 0;
		/**
		 * @brief Changes the rate the device mixes at, if the backend has one to change
		 *
		 * Only the software-mixing backends whose cost is linear in this rate honour it (the PSP's, where the
		 * mix is upsampled to the hardware's fixed rate, and the Amiga's, where AHI resamples the output);
		 * @ref nativeFrequency() then reports the new rate, so the module music decoders that size themselves
		 * by it follow on the next stream they open. A rate the backend cannot run at, or `0`, is ignored.
		 * Everywhere else this is a no-op.
		 */
		virtual void setMixingFrequency(std::int32_t frequency) {
			static_cast<void>(frequency);
		}

		/** @{ @name Buffers */

		/** @brief Creates an empty backend buffer, returning its id or `0` on failure */
		virtual std::uint32_t createBuffer(BufferUsage usage) = 0;
		/** @brief Destroys a buffer previously returned by @ref createBuffer() */
		virtual void deleteBuffer(std::uint32_t bufferId) = 0;
		/** @brief Replaces the contents of a buffer with the specified samples */
		virtual bool uploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency) = 0;

		/** @} */

		/** @{ @name Sources */

		/** @brief Attaches a buffer to a source for non-streamed playback, `0` detaches the current one */
		virtual void setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId) = 0;
		/** @brief Sets the gain of a source */
		virtual void setSourceGain(std::uint32_t sourceId, float gain) = 0;
		/** @brief Sets the pitch of a source, as a multiplier of its natural playback rate */
		virtual void setSourcePitch(std::uint32_t sourceId, float pitch) = 0;
		/** @brief Sets whether a source repeats its attached buffer */
		virtual void setSourceLooping(std::uint32_t sourceId, bool looping) = 0;
		/** @brief Sets whether the position of a source is relative to the listener */
		virtual void setSourceRelative(std::uint32_t sourceId, bool relative) = 0;
		/** @brief Sets the position of a source, in physical units */
		virtual void setSourcePosition(std::uint32_t sourceId, const Vector3f& position) = 0;
		/** @brief Sets the low-pass amount of a source, `1.0f` disables the filter */
		virtual void setSourceLowPass(std::uint32_t sourceId, float value) = 0;

		/** @brief Returns the playback position of a source in samples */
		virtual std::int32_t sourceSampleOffset(std::uint32_t sourceId) = 0;
		/** @brief Sets the playback position of a source in samples */
		virtual void setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset) = 0;

		/** @brief Starts or resumes a source */
		virtual void playSource(std::uint32_t sourceId) = 0;
		/** @brief Pauses a source at its current position */
		virtual void pauseSource(std::uint32_t sourceId) = 0;
		/** @brief Stops a source */
		virtual void stopSource(std::uint32_t sourceId) = 0;
		/** @brief Returns `true` if a source is still producing sound */
		virtual bool isSourcePlaying(std::uint32_t sourceId) = 0;

		/** @} */

		/** @{ @name Streaming */

		/** @brief Appends a buffer to the streaming queue of a source */
		virtual void queueBuffer(std::uint32_t sourceId, std::uint32_t bufferId) = 0;
		/** @brief Returns the number of queued buffers a source has finished playing */
		virtual std::int32_t numProcessedBuffers(std::uint32_t sourceId) = 0;
		/**
		 * @brief Removes the specified number of played buffers from the front of the queue
		 *
		 * @param bufferIds Receives the ids of the removed buffers, must hold @p count entries
		 */
		virtual void unqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds) = 0;

		/** @} */

#if defined(WITH_LIBRETRO)
		/**
		 * @brief Renders the next block of mixed audio into the caller's buffer (16-bit interleaved stereo)
		 *
		 * @return `false` when the device cannot render on demand and the caller has to supply silence
		 */
		virtual bool renderSamples(std::int16_t* buffer, std::int32_t numFrames) = 0;
#endif

		/** @brief Suspends the audio device */
		virtual void suspendDevice() = 0;
		/** @brief Resumes the audio device */
		virtual void resumeDevice() = 0;
	};

	inline IAudioDevice::~IAudioDevice() { }

#ifndef DOXYGEN_GENERATING_OUTPUT
	// Silent fallback device used when no audio backend is available
	class NullAudioDevice : public IAudioDevice
	{
	public:
		bool isValid() const override {
			return false;
		}

		const char* name() const override {
			return "NullAudioDevice";
		}

		float gain() const override {
			return 1.0f;
		}
		void setGain(float gain) override { }

		std::uint32_t maxNumPlayers() const override {
			return 0;
		}
		std::uint32_t numPlayers() const override {
			return 0;
		}
		const IAudioPlayer* player(std::uint32_t index) const override {
			return nullptr;
		}
		IAudioPlayer* player(std::uint32_t index) override {
			return nullptr;
		}

		void stopPlayers() override { }
		void pausePlayers() override { }
		void stopPlayers(PlayerType playerType) override { }
		void pausePlayers(PlayerType playerType) override { }

		void freezePlayers() override { }
		void unfreezePlayers() override { }

		std::uint32_t registerPlayer(IAudioPlayer* player) override { return UnavailableSource; }
		void unregisterPlayer(IAudioPlayer* player) override { }
		void updatePlayers() override {}
		bool submitStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request) override { return false; }
		void drainStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request) override { }
		const Vector3f& getListenerPosition() const override { return Vector3f::Zero; }
		void updateListener(const Vector3f& position, const Vector3f& velocity) override { }
		std::int32_t nativeFrequency() override { return 0; }

		std::uint32_t createBuffer(BufferUsage usage) override { return 0; }
		void deleteBuffer(std::uint32_t bufferId) override { }
		bool uploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency) override { return false; }

		void setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId) override { }
		void setSourceGain(std::uint32_t sourceId, float gain) override { }
		void setSourcePitch(std::uint32_t sourceId, float pitch) override { }
		void setSourceLooping(std::uint32_t sourceId, bool looping) override { }
		void setSourceRelative(std::uint32_t sourceId, bool relative) override { }
		void setSourcePosition(std::uint32_t sourceId, const Vector3f& position) override { }
		void setSourceLowPass(std::uint32_t sourceId, float value) override { }
		std::int32_t sourceSampleOffset(std::uint32_t sourceId) override { return 0; }
		void setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset) override { }
		void playSource(std::uint32_t sourceId) override { }
		void pauseSource(std::uint32_t sourceId) override { }
		void stopSource(std::uint32_t sourceId) override { }
		bool isSourcePlaying(std::uint32_t sourceId) override { return false; }

		void queueBuffer(std::uint32_t sourceId, std::uint32_t bufferId) override { }
		std::int32_t numProcessedBuffers(std::uint32_t sourceId) override { return 0; }
		void unqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds) override { }

#if defined(WITH_LIBRETRO)
		bool renderSamples(std::int16_t* buffer, std::int32_t numFrames) override { return false; }
#endif

		void suspendDevice() override { }
		void resumeDevice() override { }
	};
#endif
}
