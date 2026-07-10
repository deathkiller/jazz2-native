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
		
		Manages the listener, the pool of audio sources and all active players. Implemented by
		@ref ALAudioDevice on top of OpenAL and by @ref NullAudioDevice as a silent fallback.
	*/
	class IAudioDevice
	{
	public:
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

		void suspendDevice() override { }
		void resumeDevice() override { }
	};
#endif
}
