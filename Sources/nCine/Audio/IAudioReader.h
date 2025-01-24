#pragma once

#include "../../Main.h"

namespace nCine
{
	/// Audio reader interface
	class IAudioReader
	{
	public:
		virtual ~IAudioReader() { }

		/// Decodes audio data in a specified buffer
		/*!
		 * \param buffer Buffer pointer
		 * \param bufferSize Buffer size in bytes
		 * \return Number of bytes read
		 */
		virtual std::int32_t read(void* buffer, std::int32_t bufferSize) const = 0;

		/// Resets the audio file seek value
		virtual void rewind() const = 0;

		/// Enables native stream looping if supported
		virtual void setLooping(bool value) { }
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	/// A class created when the audio file extension is not recognized.
	class InvalidAudioReader : IAudioReader
	{
	public:
		inline std::int32_t read(void* buffer, std::int32_t bufferSize) const override { return 0; }
		inline void rewind() const override { };
	};
#endif
}
