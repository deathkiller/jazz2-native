#pragma once

#include "IAudioDevice.h"
#include "../Base/Object.h"
#include "../Primitives/Vector3.h"

namespace nCine
{
	/**
		@brief Interface for an audio player

		Common base for @ref AudioBufferPlayer and @ref AudioStreamPlayer. Wraps a single source
		of the current @ref IAudioDevice and exposes playback control together with gain, pitch,
		low-pass and 3D positioning.
	*/
	class IAudioPlayer : public Object
	{
		DEATH_RUNTIME_OBJECT();

		friend class AudioDeviceBase;

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

		/** @brief Returns the backend id of the source */
		inline std::uint32_t sourceId() const {
			return _sourceId;
		}

		/** @brief Returns the backend id of the currently playing buffer */
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
			return _state;
		}
		/** @brief Returns `true` if the player is currently playing */
		/**
		 * @brief Returns whether the player is configured so that nothing could be heard from it
		 *
		 * Every volume the user can set is folded into the player's gain by the caller, so a gain of zero
		 * is how both "the music slider is down" and "the master slider is down" reach the engine. It is
		 * worth acting on rather than mixing silence.
		 */
		inline bool isSilent() const {
			// Written so that a NaN gain counts as silent rather than as audible
			return !(_gain > 0.0f);
		}

		inline bool isPlaying() const {
			return _state == PlayerState::Playing;
		}
		/** @brief Returns `true` if the player is paused */
		inline bool isPaused() const {
			return _state == PlayerState::Paused;
		}
		/** @brief Returns `true` if the player is stopped */
		inline bool isStopped() const {
			return _state == PlayerState::Stopped;
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
			return _gain;
		}
		/** @brief Sets the player gain */
		virtual void setGain(float gain);

		/** @brief Returns the player pitch */
		inline float pitch() const {
			return _pitch;
		}
		/** @brief Sets the player pitch */
		virtual void setPitch(float pitch);

		/** @brief Returns the player low-pass amount */
		inline float lowPass() const {
			return _lowPass;
		}
		/** @brief Sets the player low-pass amount */
		virtual void setLowPass(float value);

		/** @brief Returns the player position */
		inline Vector3f position() const {
			return _position;
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

		/** @brief Backend source id */
		std::uint32_t _sourceId;
		/** @brief Current playback state */
		PlayerState _state;
		/** @brief Player flags */
		PlayerFlags _flags;
		/** @brief Gain */
		float _gain;
		/** @brief Pitch */
		float _pitch;
		/** @brief Low-pass amount */
		float _lowPass;
		/** @brief Position in space */
		Vector3f _position;

		constexpr bool GetFlags(PlayerFlags flag) const noexcept {
			return (_flags & flag) == flag;
		}

		constexpr void SetFlags(PlayerFlags flag, bool value) noexcept {
			if (value) {
				_flags = _flags | flag;
			} else {
				_flags = _flags & (~flag);
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
