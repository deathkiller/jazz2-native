#pragma once

#include "IAudioPlayer.h"
#include "AudioStream.h"

namespace nCine
{
	/// Audio stream player
	class AudioStreamPlayer : public IAudioPlayer
	{
		DEATH_RUNTIME_OBJECT(IAudioPlayer);

	public:
		/// Default constructor
		AudioStreamPlayer();
		/// A constructor creating a player from a file
		explicit AudioStreamPlayer(StringView filename);
		~AudioStreamPlayer() override;

		AudioStreamPlayer(const AudioStreamPlayer&) = delete;
		AudioStreamPlayer& operator=(const AudioStreamPlayer&) = delete;
		AudioStreamPlayer(AudioStreamPlayer&&) = default;
		AudioStreamPlayer& operator=(AudioStreamPlayer&&) = default;

		bool loadFromFile(const char* filename);

		inline std::uint32_t bufferId() const override {
			return audioStream_.bufferId();
		}

		inline std::int32_t bytesPerSample() const override {
			return audioStream_.bytesPerSample();
		}
		inline std::int32_t numChannels() const override {
			return audioStream_.numChannels();
		}
		inline std::int32_t frequency() const override {
			return audioStream_.frequency();
		}

		inline std::int32_t numSamples() const override {
			return audioStream_.numSamples();
		}
		inline float duration() const override {
			return audioStream_.duration();
		}

		inline std::int32_t bufferSize() const override {
			return audioStream_.bufferSize();
		}

		inline std::int32_t numStreamSamples() const {
			return audioStream_.numStreamSamples();
		}
		inline std::int32_t streamBufferSize() const {
			return audioStream_.streamBufferSize();
		}

		void play() override;
		void pause() override;
		void stop() override;
		void setLooping(bool value) override;

		/// Updates the player state and the stream buffer queue
		void updateState() override;

		inline static ObjectType sType() {
			return ObjectType::AudioStreamPlayer;
		}

	private:
		AudioStream audioStream_;
	};
}
