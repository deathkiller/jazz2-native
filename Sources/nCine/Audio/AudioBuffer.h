#pragma once

#include "../Base/Object.h"

#include <Containers/StringView.h>
#include <IO/Stream.h>

using namespace Death::Containers;

namespace nCine
{
	class IAudioLoader;

	/// OpenAL audio buffer
	/*! It inherits from `Object` because a buffer can be
	 *  shared by more than one `AudioBufferPlayer` object. */
	class AudioBuffer : public Object
	{
	public:
		enum class Format {
			Mono8,
			Stereo8,
			Mono16,
			Stereo16
		};

		/// Creates an OpenAL buffer name
		AudioBuffer();
		/// A constructor creating a buffer from a file
		explicit AudioBuffer(StringView filename);
		AudioBuffer(std::unique_ptr<Death::IO::Stream> fileHandle, StringView filename);
		~AudioBuffer() override;

		AudioBuffer(const AudioBuffer&) = delete;
		AudioBuffer& operator=(const AudioBuffer&) = delete;
		AudioBuffer(AudioBuffer&& other) noexcept;
		AudioBuffer& operator=(AudioBuffer&& other) noexcept;

		/// Initializes an empty buffer with the specified format and frequency
		void init(Format format, std::int32_t frequency);

		bool loadFromFile(StringView filename);
		bool loadFromStream(std::unique_ptr<Death::IO::Stream> fileHandle, StringView filename);
		/// Loads samples in raw PCM format from a memory buffer
		bool loadFromSamples(const unsigned char* bufferPtr, std::int32_t bufferSize);

		/// Returns the OpenAL buffer id
		inline std::uint32_t bufferId() const {
			return bufferId_;
		}

		/// Returns the number of bytes per sample
		inline std::int32_t bytesPerSample() const {
			return bytesPerSample_;
		}
		/// Returns the number of audio channels
		inline std::int32_t numChannels() const {
			return numChannels_;
		}
		/// Returns the samples frequency
		inline std::int32_t frequency() const {
			return frequency_;
		}

		/// Returns number of samples
		inline std::int32_t numSamples() const {
			return numSamples_;
		}
		/// Returns the duration in seconds
		inline float duration() const {
			return duration_;
		}

		/// Returns the size of the buffer in bytes
		inline std::int32_t bufferSize() const {
			return numSamples_ * numChannels_ * bytesPerSample_;
		}

		inline static ObjectType sType() {
			return ObjectType::AudioBuffer;
		}

	private:
		/// The OpenAL buffer id
		std::uint32_t bufferId_;

		/// Number of bytes per sample
		std::int32_t bytesPerSample_;
		/// Number of channels
		std::int32_t numChannels_;
		/// Samples frequency
		std::int32_t frequency_;

		/// Number of samples
		std::int32_t numSamples_;
		/// Duration in seconds
		float duration_;

		/// Loads audio samples based on information from the audio loader and reader
		bool load(IAudioLoader& audioLoader);
	};
}
