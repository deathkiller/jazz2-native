#pragma once

#include "IAudioDevice.h"
#include "../Base/Object.h"
#include "../Primitives/Vector3.h"

namespace nCine
{
	/**
		@brief Interface for an audio player
		
		Common base for @ref AudioBufferPlayer and @ref AudioStreamPlayer. Wraps a single OpenAL
		source and exposes playback control together with gain, pitch, low-pass and 3D positioning.
	*/
	class IAudioPlayer : public Object
	{
		DEATH_RUNTIME_OBJECT();

		friend class ALAudioDevice;

	public:
		/** @brief Playback state */
		enum class PlayerState {
			Initial = 0,	/**< Created but never played */
			Playing,		/**< Currently playing */
			Paused,			/**< Paused, can be resumed from the current position */
			Stopped			/**< Stopped and rewound to the beginning */
		};

		IAudioPlayer(ObjectType type);
		~IAudioPlayer() override;

		IAudioPlayer(IAudioPlayer&&) = default;
		IAudioPlayer& operator=(IAudioPlayer&&) = default;

		/** @brief Returns the OpenAL id of the source */
		inline std::uint32_t sourceId() const {
			return sourceId_;
		}

		/** @brief Returns the OpenAL id of the currently playing buffer */
		virtual std::uint32_t bufferId() const = 0;

		/** @brief Returns the number of bytes per sample */
		virtual std::int32_t bytesPerSample() const = 0;
		/** @brief Returns the number of audio channels of the currently playing buffer */
		virtual std::int32_t numChannels() const = 0;
		/** @brief Returns the sample frequency of the currently playing buffer */
		virtual std::int32_t frequency() const = 0;

		/** @brief Returns the total number of samples */
		virtual std::int32_t numSamples() const = 0;
		/** @brief Returns the duration in seconds */
		virtual float duration() const = 0;

		/** @brief Returns the size of the currently playing buffer in bytes */
		virtual std::int32_t bufferSize() const = 0;

		/** @brief Returns the current playback position in samples */
		virtual std::int32_t sampleOffset() const;
		/** @brief Sets the current playback position in samples */
		virtual void setSampleOffset(std::int32_t offset);

		/** @brief Starts or resumes playback */
		virtual void play() = 0;
		/** @brief Pauses playback at the current position */
		virtual void pause() = 0;
		/** @brief Stops playback and rewinds to the beginning */
		virtual void stop() = 0;

		/** @brief Returns the current playback state */
		inline PlayerState state() const {
			return state_;
		}
		/** @brief Returns `true` if the player is currently playing */
		inline bool isPlaying() const {
			return state_ == PlayerState::Playing;
		}
		/** @brief Returns `true` if the player is paused */
		inline bool isPaused() const {
			return state_ == PlayerState::Paused;
		}
		/** @brief Returns `true` if the player is stopped */
		inline bool isStopped() const {
			return state_ == PlayerState::Stopped;
		}

		/** @brief Returns `true` if the player is looping */
		inline bool isLooping() const {
			return GetFlags(PlayerFlags::Looping);
		}
		/** @brief Sets whether the player should loop */
		virtual void setLooping(bool value) {
			SetFlags(PlayerFlags::Looping, value);
		}
		/** @brief Returns `true` if the position is relative to the listener */
		inline bool isSourceRelative() const {
			return GetFlags(PlayerFlags::SourceRelative);
		}
		/** @brief Sets whether the position is relative to the listener */
		virtual void setSourceRelative(bool value);

		/** @brief Returns `true` if the player is forced to behave as a 2D source */
		inline bool isAs2D() const {
			return GetFlags(PlayerFlags::As2D);
		}
		/** @brief Sets whether the player should behave as a 2D source */
		virtual void setAs2D(bool value) {
			SetFlags(PlayerFlags::As2D, value);
		}

		/** @brief Returns the player gain */
		inline float gain() const {
			return gain_;
		}
		/** @brief Sets the player gain */
		virtual void setGain(float gain);

		/** @brief Returns the player pitch */
		inline float pitch() const {
			return pitch_;
		}
		/** @brief Sets the player pitch */
		virtual void setPitch(float pitch);

		/** @brief Returns the player low-pass amount */
		inline float lowPass() const {
			return lowPass_;
		}
		/** @brief Sets the player low-pass amount */
		virtual void setLowPass(float value);

		/** @brief Returns the player position */
		inline Vector3f position() const {
			return position_;
		}
		/** @brief Sets the player position */
		virtual void setPosition(const Vector3f& position);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		enum class PlayerFlags {
			None = 0,
			Looping = 0x01,
			SourceRelative = 0x02,
			As2D = 0x04
		};

		DEATH_PRIVATE_ENUM_FLAGS(PlayerFlags);

		/** @brief OpenAL source id */
		std::uint32_t sourceId_;
		/** @brief Current playback state */
		PlayerState state_;
		/** @brief Player flags */
		PlayerFlags flags_;
		/** @brief Gain */
		float gain_;
		/** @brief Pitch */
		float pitch_;
		/** @brief Low-pass amount */
		float lowPass_;
		/** @brief Position in space */
		Vector3f position_;
		/** @brief OpenAL low-pass filter handle */
		std::uint32_t filterHandle_;

		constexpr bool GetFlags(PlayerFlags flag) const noexcept {
			return (flags_ & flag) == flag;
		}

		constexpr void SetFlags(PlayerFlags flag, bool value) noexcept {
			if (value) {
				flags_ = flags_ | flag;
			} else {
				flags_ = flags_ & (~flag);
			}
		}
#endif

		/**
		 * @brief Updates the player state once the source has finished playing
		 *
		 * Called every frame by @ref IAudioDevice. In stream players it is also responsible
		 * for queueing and unqueueing buffers.
		 */
		virtual void updateState() = 0;

		virtual void updateFilters();

		virtual Vector3f getAdjustedPosition(IAudioDevice& device, const Vector3f& pos, bool isSourceRelative, bool isAs2D);

		void setPositionInternal(const Vector3f& position);
	};
}
