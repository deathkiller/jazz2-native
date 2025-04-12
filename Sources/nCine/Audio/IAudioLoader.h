#pragma once

#include "IAudioReader.h"

#include <memory>

#include <Containers/StringView.h>
#include <IO/Stream.h>

namespace nCine
{
	/// Audio loader interface
	class IAudioLoader
	{
	public:
		virtual ~IAudioLoader() { }

		/// Returns true if the audio has been correctly loaded
		inline bool hasLoaded() const {
			return hasLoaded_;
		}

		/// Returns number of bytes per sample
		inline std::int32_t bytesPerSample() const {
			return bytesPerSample_;
		}
		/// Returns number of channels
		inline std::int32_t numChannels() const {
			return numChannels_;
		}
		/// Returns samples frequency
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

		/// Returns the decoded buffer size in bytes
		inline std::int32_t bufferSize() const {
			return numSamples_ * numChannels_ * bytesPerSample_;
		}

		/// Returns the proper audio loader according to the file extension
		static std::unique_ptr<IAudioLoader> createFromFile(const Death::Containers::StringView path);
		static std::unique_ptr<IAudioLoader> createFromStream(std::unique_ptr<Death::IO::Stream> fileHandle, Death::Containers::StringView path);

		/// Returns the proper audio reader according to the loader instance
		virtual std::unique_ptr<IAudioReader> createReader() = 0;

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		/// A flag indicating if the loading process has been successful
		bool hasLoaded_;
		/// Audio file handle
		std::unique_ptr<Death::IO::Stream> fileHandle_;

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
#endif

		explicit IAudioLoader(std::unique_ptr<Death::IO::Stream> fileHandle);

		static std::unique_ptr<IAudioLoader> createLoader(std::unique_ptr<Death::IO::Stream> fileHandle, Death::Containers::StringView path);
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	/// A class created when the audio file extension is not recognized
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
