#pragma once

#include "../Base/Object.h"

#include <Containers/StringView.h>
#include <IO/Stream.h>

using namespace Death::Containers;

namespace nCine
{
	class IAudioLoader;

	/**
		@brief Fully decoded OpenAL audio buffer
		
		Holds an entire sound effect decoded into memory. Inherits from @ref Object so a single
		buffer can be shared by multiple @ref AudioBufferPlayer instances.
	*/
	class AudioBuffer : public Object
	{
	public:
		/** @brief Sample format */
		enum class Format {
			Mono8,		/**< 8-bit, single channel */
			Stereo8,	/**< 8-bit, two channels */
			Mono16,		/**< 16-bit, single channel */
			Stereo16	/**< 16-bit, two channels */
		};

		/** @brief Creates an empty OpenAL buffer */
		AudioBuffer();
		/** @brief Creates a buffer and loads it from the specified file */
		explicit AudioBuffer(StringView filename);
		/** @brief Creates a buffer and loads it from an already opened stream */
		AudioBuffer(std::unique_ptr<Death::IO::Stream> fileHandle, StringView filename);
		~AudioBuffer() override;

		AudioBuffer(const AudioBuffer&) = delete;
		AudioBuffer& operator=(const AudioBuffer&) = delete;
		AudioBuffer(AudioBuffer&& other) noexcept;
		AudioBuffer& operator=(AudioBuffer&& other) noexcept;

		/** @brief Initializes an empty buffer with the specified format and frequency */
		void init(Format format, std::int32_t frequency);

		/** @brief Loads audio data from the specified file */
		bool loadFromFile(StringView filename);
		/** @brief Loads audio data from an already opened stream */
		bool loadFromStream(std::unique_ptr<Death::IO::Stream> fileHandle, StringView filename);
		/** @brief Loads samples in raw PCM format from a memory buffer */
		bool loadFromSamples(const unsigned char* bufferPtr, std::int32_t bufferSize);

		/** @brief Returns the OpenAL buffer id */
		inline std::uint32_t bufferId() const {
			return bufferId_;
		}

		/** @brief Returns the number of bytes per sample */
		inline std::int32_t bytesPerSample() const {
			return bytesPerSample_;
		}
		/** @brief Returns the number of audio channels */
		inline std::int32_t numChannels() const {
			return numChannels_;
		}
		/** @brief Returns the sample frequency */
		inline std::int32_t frequency() const {
			return frequency_;
		}

		/** @brief Returns the total number of samples */
		inline std::int32_t numSamples() const {
			return numSamples_;
		}
		/** @brief Returns the duration in seconds */
		inline float duration() const {
			return duration_;
		}

		/** @brief Returns the size of the buffer in bytes */
		inline std::int32_t bufferSize() const {
			return numSamples_ * numChannels_ * bytesPerSample_;
		}

		/** @brief Returns the static object type of this class */
		inline static ObjectType sType() {
			return ObjectType::AudioBuffer;
		}

	private:
		/** @brief OpenAL buffer id */
		std::uint32_t bufferId_;

		/** @brief Number of bytes per sample */
		std::int32_t bytesPerSample_;
		/** @brief Number of channels */
		std::int32_t numChannels_;
		/** @brief Sample frequency */
		std::int32_t frequency_;

		/** @brief Number of samples */
		std::int32_t numSamples_;
		/** @brief Duration in seconds */
		float duration_;

		/** @brief Loads audio samples using the supplied loader and its reader */
		bool load(IAudioLoader& audioLoader);
	};
}
