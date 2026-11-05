#pragma once

#include "IAudioReader.h"

#include <memory>

#include <Containers/StringView.h>
#include <IO/Stream.h>

namespace nCine
{
	/**
		@brief Interface for an audio loader
		
		Opens an audio file and reads its format. Use @ref CreateFromFile() or @ref CreateFromStream(),
		then @ref CreateReader() to decode the samples.
	*/
	class IAudioLoader
	{
	public:
		virtual ~IAudioLoader() { }

		/** @brief Returns `true` if the audio file was loaded successfully */
		inline bool HasLoaded() const {
			return _hasLoaded;
		}

		/** @brief Returns the number of bytes per sample */
		inline std::int32_t GetBytesPerSample() const {
			return _bytesPerSample;
		}
		/** @brief Returns the number of channels */
		inline std::int32_t GetChannelCount() const {
			return _numChannels;
		}
		/** @brief Returns the sample frequency */
		inline std::int32_t GetFrequency() const {
			return _frequency;
		}

		/** @brief Returns the total number of samples */
		inline std::int32_t GetSampleCount() const {
			return _numSamples;
		}
		/** @brief Returns the duration in seconds */
		inline float GetDuration() const {
			return _duration;
		}

		/** @brief Returns the decoded buffer size in bytes */
		inline std::int32_t GetBufferSize() const {
			return _numSamples * _numChannels * _bytesPerSample;
		}

		/** @brief Creates the loader matching the extension of the specified file */
		static std::unique_ptr<IAudioLoader> CreateFromFile(const Death::Containers::StringView path);
		/** @brief Creates the loader matching the specified file for an already opened stream */
		static std::unique_ptr<IAudioLoader> CreateFromStream(std::unique_ptr<Death::IO::Stream> fileHandle, Death::Containers::StringView path);

		/** @brief Creates the audio reader matching this loader */
		virtual std::unique_ptr<IAudioReader> CreateReader() = 0;

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		/** @brief Whether the loading process was successful */
		bool _hasLoaded;
		/** @brief Audio file handle */
		std::unique_ptr<Death::IO::Stream> _fileHandle;

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
#endif

		explicit IAudioLoader(std::unique_ptr<Death::IO::Stream> fileHandle);

		static std::unique_ptr<IAudioLoader> CreateLoader(std::unique_ptr<Death::IO::Stream> fileHandle, Death::Containers::StringView path);
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	// Fallback loader used when the audio file extension is not recognized
	class InvalidAudioLoader : public IAudioLoader
	{
	public:
		explicit InvalidAudioLoader()
			: IAudioLoader(nullptr) { }

		std::unique_ptr<IAudioReader> CreateReader() override {
			return nullptr;
		}
	};
#endif
}
