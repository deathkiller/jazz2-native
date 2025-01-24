#pragma once

#include "IAudioPlayer.h"

namespace nCine
{
	class AudioBuffer;

	/// Audio buffer player
	class AudioBufferPlayer : public IAudioPlayer
	{
		DEATH_RUNTIME_OBJECT(IAudioPlayer);

	public:
		/// Default constructor
		AudioBufferPlayer();
		/// A constructor creating a player from a shared buffer
		explicit AudioBufferPlayer(AudioBuffer* audioBuffer);
		~AudioBufferPlayer() override;

		AudioBufferPlayer(const AudioBufferPlayer&) = delete;
		AudioBufferPlayer& operator=(const AudioBufferPlayer&) = delete;
		AudioBufferPlayer(AudioBufferPlayer&&) = default;
		AudioBufferPlayer& operator=(AudioBufferPlayer&&) = default;

		std::uint32_t bufferId() const override;

		std::int32_t bytesPerSample() const override;
		std::int32_t numChannels() const override;
		std::int32_t frequency() const override;

		std::int32_t numSamples() const override;
		float duration() const override;

		std::int32_t bufferSize() const override;

		/// Gets the audio buffer used for playing
		inline const AudioBuffer* audioBuffer() const {
			return audioBuffer_;
		}
		/// Sets the audio buffer used for playing
		void setAudioBuffer(AudioBuffer* audioBuffer);

		void play() override;
		void pause() override;
		void stop() override;

		/// Updates the player state
		void updateState() override;

		inline static ObjectType sType() {
			return ObjectType::AudioBufferPlayer;
		}

	private:
		AudioBuffer* audioBuffer_;
	};
}
