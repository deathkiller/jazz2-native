#pragma once

#include "IAudioDevice.h"
#include "../Base/Object.h"

#include <Containers/StringView.h>
#include <IO/Stream.h>

using namespace Death::Containers;

namespace nCine
{
	class IAudioLoader;

	/**
		@brief Fully decoded audio buffer

		Holds an entire sound effect decoded into memory. Inherits from @ref Object so a single
		buffer can be shared by multiple @ref AudioBufferPlayer instances. The samples live in
		a buffer object owned by the current @ref IAudioDevice, which on some backends means
		dedicated sound memory rather than the main heap.
	*/
	class AudioBuffer : public Object
	{
	public:
		/** @brief Sample format */
		using Format = IAudioDevice::BufferFormat;

		/** @brief Creates an empty buffer */
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

		/** @brief Returns the backend buffer id */
		inline std::uint32_t bufferId() const {
			return _bufferId;
		}

		/** @brief Returns the number of bytes per sample */
		inline std::int32_t bytesPerSample() const {
			return _bytesPerSample;
		}
		/** @brief Returns the number of audio channels */
		inline std::int32_t numChannels() const {
			return _numChannels;
		}
		/** @brief Returns the sample frequency */
		inline std::int32_t frequency() const {
			return _frequency;
		}

		/** @brief Returns the total number of samples */
		inline std::int32_t numSamples() const {
			return _numSamples;
		}
		/** @brief Returns the duration in seconds */
		inline float duration() const {
			return _duration;
		}

		/** @brief Returns the size of the buffer in bytes */
		inline std::int32_t bufferSize() const {
			return _numSamples * _numChannels * _bytesPerSample;
		}

		/** @brief Returns the static object type of this class */
		inline static ObjectType sType() {
			return ObjectType::AudioBuffer;
		}

	private:
		/** @brief Backend buffer id */
		std::uint32_t _bufferId;

		/** @brief Number of bytes per sample */
		std::int32_t _bytesPerSample;
		/** @brief Number of channels */
		std::int32_t _numChannels;
		/** @brief Sample frequency */
		std::int32_t _frequency;

		/** @brief Number of samples */
		std::int32_t _numSamples;
		/** @brief Duration in seconds */
		float _duration;

		/** @brief Loads audio samples using the supplied loader and its reader */
		bool load(IAudioLoader& audioLoader);
	};
}
