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

		std::uint32_t GetBufferId() const override;

		std::int32_t GetBytesPerSample() const override;
		std::int32_t GetChannelCount() const override;
		std::int32_t GetFrequency() const override;

		std::int32_t GetSampleCount() const override;
		float GetDuration() const override;

		std::int32_t GetBufferSize() const override;

		/** @brief Returns the audio buffer used for playback */
		inline const AudioBuffer* GetAudioBuffer() const {
			return _audioBuffer;
		}
		/** @brief Sets the audio buffer used for playback */
		void SetAudioBuffer(AudioBuffer* audioBuffer);

		void Play() override;
		void Pause() override;
		void Stop() override;

		void SetLooping(bool value) override;

		void UpdateState() override;

		/** @brief Returns the static object type of this class */
		inline static ObjectType sType() {
			return ObjectType::AudioBufferPlayer;
		}

	private:
		AudioBuffer* _audioBuffer;
	};
}
