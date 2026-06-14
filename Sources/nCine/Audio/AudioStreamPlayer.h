#pragma once

#include "IAudioPlayer.h"
#include "AudioStream.h"

namespace nCine
{
	/**
		@brief Plays back an @ref AudioStream decoded on the fly
		
		Suitable for long sounds such as music. Owns the @ref AudioStream it plays and forwards
		most queries to it.
	*/
	class AudioStreamPlayer : public IAudioPlayer
	{
		DEATH_RUNTIME_OBJECT(IAudioPlayer);

	public:
		AudioStreamPlayer();
		/** @brief Creates a player and loads the stream from the specified file */
		explicit AudioStreamPlayer(StringView filename);
		~AudioStreamPlayer() override;

		AudioStreamPlayer(const AudioStreamPlayer&) = delete;
		AudioStreamPlayer& operator=(const AudioStreamPlayer&) = delete;
		AudioStreamPlayer(AudioStreamPlayer&&) = default;
		AudioStreamPlayer& operator=(AudioStreamPlayer&&) = default;

		/** @brief Loads the stream from the specified file */
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

		/** @brief Returns the number of samples held by a single streaming buffer */
		inline std::int32_t numStreamSamples() const {
			return audioStream_.numStreamSamples();
		}
		/** @brief Returns the size of a single streaming buffer in bytes */
		inline std::int32_t streamBufferSize() const {
			return audioStream_.streamBufferSize();
		}

		void play() override;
		void pause() override;
		void stop() override;
		void setLooping(bool value) override;

		void updateState() override;

		/** @brief Returns the static object type of this class */
		inline static ObjectType sType() {
			return ObjectType::AudioStreamPlayer;
		}

	private:
		AudioStream audioStream_;
	};
}
