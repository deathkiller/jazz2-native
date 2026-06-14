#pragma once

#include "../Primitives/Vector3.h"
#include "../Base/FrameTimer.h"

namespace nCine
{
	class IAudioPlayer;

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

		void stopPlayers() override { }
		void pausePlayers() override { }
		void stopPlayers(PlayerType playerType) override { }
		void pausePlayers(PlayerType playerType) override { }

		void freezePlayers() override { }
		void unfreezePlayers() override { }

		std::uint32_t registerPlayer(IAudioPlayer* player) override { return UnavailableSource; }
		void unregisterPlayer(IAudioPlayer* player) override { }
		void updatePlayers() override {}
		const Vector3f& getListenerPosition() const override { return Vector3f::Zero; }
		void updateListener(const Vector3f& position, const Vector3f& velocity) override { }
		std::int32_t nativeFrequency() override { return 0; }

		void suspendDevice() override { }
		void resumeDevice() override { }
	};
#endif
}
