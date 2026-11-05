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
		bool LoadFromFile(const char* filename);

		inline std::uint32_t GetBufferId() const override {
			return _audioStream.GetBufferId();
		}

		inline std::int32_t GetBytesPerSample() const override {
			return _audioStream.GetBytesPerSample();
		}
		inline std::int32_t GetChannelCount() const override {
			return _audioStream.GetChannelCount();
		}
		inline std::int32_t GetFrequency() const override {
			return _audioStream.GetFrequency();
		}

		inline std::int32_t GetSampleCount() const override {
			return _audioStream.GetSampleCount();
		}
		inline float GetDuration() const override {
			return _audioStream.GetDuration();
		}

		inline std::int32_t GetBufferSize() const override {
			return _audioStream.GetBufferSize();
		}

#if !defined(NCINE_HAS_NATIVE_AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Returns the number of samples held by a single streaming buffer */
		inline std::int32_t GetStreamSampleCount() const {
			return _audioStream.GetStreamSampleCount();
		}
		/** @brief Returns the size of a single streaming buffer in bytes */
		inline std::int32_t GetStreamBufferSize() const {
			return _audioStream.GetStreamBufferSize();
		}
#endif

		void Play() override;
		void Pause() override;
		void Stop() override;
		void SetLooping(bool value) override;

		void UpdateState() override;

		/**
		 * @brief Returns the state of a tracker module at the position being heard, `false` if the stream isn't a module
		 *
		 * @partialsupport Not every decoder supports it, always returns `false` where `NCINE_HAS_NATIVE_AUDIO` is defined.
		 */
		inline bool GetModuleState(AudioModuleState& state) const {
			return _audioStream.GetModuleState(state);
		}
		/**
		 * @brief Jumps to a row of a position in the order list of a tracker module
		 *
		 * While playing, the jump is heard at once. Otherwise the module continues from there once it plays.
		 *
		 * @partialsupport Not every decoder supports it, has no effect where `NCINE_HAS_NATIVE_AUDIO` is defined.
		 */
		bool SetModulePosition(std::int32_t order, std::int32_t row);
		/**
		 * @brief Sets the ticks per row of a tracker module until the module changes them
		 *
		 * @partialsupport Not every decoder supports it, has no effect where `NCINE_HAS_NATIVE_AUDIO` is defined.
		 */
		inline bool SetModuleSpeed(std::int32_t speed) {
			return _audioStream.SetModuleSpeed(speed);
		}
		/**
		 * @brief Sets the tempo of a tracker module until the module changes it
		 *
		 * @partialsupport Not every decoder supports it, has no effect where `NCINE_HAS_NATIVE_AUDIO` is defined.
		 */
		inline bool SetModuleTempo(std::int32_t tempo) {
			return _audioStream.SetModuleTempo(tempo);
		}
		/**
		 * @brief Changes the volume of a channel of a tracker module gradually, @p volume is in `0.0` to `1.0`
		 *
		 * @partialsupport Not every decoder supports it, has no effect where `NCINE_HAS_NATIVE_AUDIO` is defined.
		 */
		inline bool SlideModuleChannelVolume(std::int32_t channel, float volume, std::int32_t milliseconds) {
			return _audioStream.SlideModuleChannelVolume(channel, volume, milliseconds);
		}

		/** @brief Returns the static object type of this class */
		inline static ObjectType sType() {
			return ObjectType::AudioStreamPlayer;
		}

	private:
		AudioStream _audioStream;
	};
}
