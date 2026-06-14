#pragma once

#include "IAudioReader.h"

#include <memory>

#include <Containers/StringView.h>
#include <IO/Stream.h>

namespace nCine
{
	/**
		@brief Interface for an audio loader
		
		Opens an audio file, reads its header and exposes the format metadata. Use one of the
		`create*()` factory methods to obtain the loader matching a given file, then call
		@ref createReader() to decode the actual samples.
	*/
	class IAudioLoader
	{
	public:
		virtual ~IAudioLoader() { }

		/** @brief Returns `true` if the audio file was loaded successfully */
		inline bool hasLoaded() const {
			return hasLoaded_;
		}

		/** @brief Returns the number of bytes per sample */
		inline std::int32_t bytesPerSample() const {
			return bytesPerSample_;
		}
		/** @brief Returns the number of channels */
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

		/** @brief Returns the decoded buffer size in bytes */
		inline std::int32_t bufferSize() const {
			return numSamples_ * numChannels_ * bytesPerSample_;
		}

		/** @brief Creates the loader matching the extension of the specified file */
		static std::unique_ptr<IAudioLoader> createFromFile(const Death::Containers::StringView path);
		/** @brief Creates the loader matching the specified file for an already opened stream */
		static std::unique_ptr<IAudioLoader> createFromStream(std::unique_ptr<Death::IO::Stream> fileHandle, Death::Containers::StringView path);

		/** @brief Creates the audio reader matching this loader */
		virtual std::unique_ptr<IAudioReader> createReader() = 0;

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		/** @brief Whether the loading process was successful */
		bool hasLoaded_;
		/** @brief Audio file handle */
		std::unique_ptr<Death::IO::Stream> fileHandle_;

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
#endif

		explicit IAudioLoader(std::unique_ptr<Death::IO::Stream> fileHandle);

		static std::unique_ptr<IAudioLoader> createLoader(std::unique_ptr<Death::IO::Stream> fileHandle, Death::Containers::StringView path);
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	// Fallback loader used when the audio file extension is not recognized
	class InvalidAudioLoader : public IAudioLoader
	{
	public:
		explicit InvalidAudioLoader()
			: IAudioLoader(nullptr) { }

		std::unique_ptr<IAudioReader> createReader() override {
			return nullptr;
		}
	};
#endif
}
