#pragma once

#include "IAudioPlayer.h"

namespace nCine
{
	class AudioBuffer;

	/**
		@brief Plays back a fully decoded @ref AudioBuffer
		
		Suitable for short sound effects that are decoded into memory ahead of time. The same
		buffer can be shared between several players.
	*/
	class AudioBufferPlayer : public IAudioPlayer
	{
		DEATH_RUNTIME_OBJECT(IAudioPlayer);

	public:
		AudioBufferPlayer();
		/** @brief Creates a player bound to the specified shared buffer */
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

		/** @brief Returns the audio buffer used for playback */
		inline const AudioBuffer* audioBuffer() const {
			return audioBuffer_;
		}
		/** @brief Sets the audio buffer used for playback */
		void setAudioBuffer(AudioBuffer* audioBuffer);

		void play() override;
		void pause() override;
		void stop() override;

		void updateState() override;

		/** @brief Returns the static object type of this class */
		inline static ObjectType sType() {
			return ObjectType::AudioBufferPlayer;
		}

	private:
		AudioBuffer* audioBuffer_;
	};
}
