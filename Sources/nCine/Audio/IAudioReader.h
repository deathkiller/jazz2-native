#pragma once

#include "../../Main.h"

namespace nCine
{
	/**
		@brief Interface for an audio reader
		
		Decodes audio samples on demand from a file opened by an @ref IAudioLoader. Stream players
		call @ref read() repeatedly to refill their buffer queue.
	*/
	class IAudioReader
	{
	public:
		virtual ~IAudioReader() { }

		/**
		 * @brief Decodes audio data into the specified buffer
		 *
		 * @param buffer		Destination buffer
		 * @param bufferSize	Buffer size in bytes
		 * @return Number of bytes actually written
		 */
		virtual std::int32_t read(void* buffer, std::int32_t bufferSize) const = 0;

		/** @brief Rewinds the reader back to the beginning */
		virtual void rewind() const = 0;

		/** @brief Enables native stream looping if the format supports it */
		virtual void setLooping(bool value) { }
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	// Fallback reader used when the audio file extension is not recognized
	class InvalidAudioReader : IAudioReader
	{
	public:
		inline std::int32_t read(void* buffer, std::int32_t bufferSize) const override { return 0; }
		inline void rewind() const override { };
	};
#endif
}
