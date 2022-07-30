#pragma once

#include "../Base/Object.h"
#include "../Primitives/Vector3.h"

namespace nCine
{
	/// Audio player interface class
	class IAudioPlayer : public Object
	{
	public:
		/// Player state
		enum class PlayerState
		{
			Initial = 0,
			Playing,
			Paused,
			Stopped
		};

		IAudioPlayer(ObjectType type);
		~IAudioPlayer() override;

		/// Default move constructor
		IAudioPlayer(IAudioPlayer&&) = default;
		/// Default move assignment operator
		IAudioPlayer& operator=(IAudioPlayer&&) = default;

		/// Returns the OpenAL id of the player source
		inline unsigned int sourceId() const {
			return sourceId_;
		}
		/// Returns the OpenAL id of the currently playing buffer
		virtual unsigned int bufferId() const = 0;

		/// Returns the number of bytes per sample
		virtual int bytesPerSample() const = 0;
		/// Returns the number of audio channels of the currently playing buffer
		virtual int numChannels() const = 0;
		/// Returns the samples frequency of the currently playing buffer
		virtual int frequency() const = 0;

		/// Returns the number of samples
		virtual unsigned long int numSamples() const = 0;
		/// Returns the duration in seconds
		virtual float duration() const = 0;

		/// Returns the size of the currently playing buffer in bytes
		virtual unsigned long bufferSize() const = 0;

		/// Returns the playback position expressed in samples
		int sampleOffset() const;
		/// Sets the playback position expressed in samples
		void setSampleOffset(int offset);

		/// Starts playing
		virtual void play() = 0;
		/// Pauses playing
		virtual void pause() = 0;
		/// Stops playing and rewind
		virtual void stop() = 0;

		/// Returns the state of the player
		inline PlayerState state() const {
			return state_;
		}
		/// Queries the playing state of the player
		inline bool isPlaying() const {
			return state_ == PlayerState::Playing;
		}
		/// Queries the paused state of the player
		inline bool isPaused() const {
			return state_ == PlayerState::Paused;
		}
		/// Queries the stopped state of the player
		inline bool isStopped() const {
			return state_ == PlayerState::Stopped;
		}

		/// Queries the looping property of the player
		inline bool isLooping() const {
			return isLooping_;
		}
		/// Sets player looping property
		virtual void setLooping(bool isLooping) {
			isLooping_ = isLooping;
		}

		/// Returns player gain value
		inline float gain() const {
			return gain_;
		}
		/// Sets player gain value
		void setGain(float gain);
		/// Returns player pitch value
		inline float pitch() const {
			return pitch_;
		}
		/// Sets player pitch value
		void setPitch(float pitch);
		/// Returns player low-pass value
		inline float lowPass() const {
			return lowPass_;
		}
		/// Sets player low-pass value
		void setLowPass(float value);
		/// Returns player position value
		inline Vector3f position() const {
			return position_;
		}
		/// Sets player position value through vector
		void setPosition(const Vector3f& position);
		/// Sets player position value through components
		void setPosition(float x, float y, float z);

	protected:
		/// The OpenAL source id
		unsigned int sourceId_;
		/// Current player state
		PlayerState state_;
		/// Looping status flag
		bool isLooping_;
		/// Player gain value
		float gain_;
		/// Player pitch value
		float pitch_;
		/// Player low-pass value
		float lowPass_;
		/// Player position in space
		Vector3f position_;
		/// Filter handle
		unsigned int filterHandle_;

		/// Updates the state of the player if the source has done playing
		/*! It is called every frame by the `IAudioDevice` class and it is
		 *  also responsible for buffer queueing/unqueueing in stream players. */
		virtual void updateState() = 0;

		virtual void updateFilters();

		friend class ALAudioDevice;
	};

}
