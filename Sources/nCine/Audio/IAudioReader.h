#pragma once

#include "../../Main.h"

namespace nCine
{
	/** @brief Position and timing of a tracker module, see @ref IAudioReader::GetModuleStateAt() */
	struct AudioModuleState
	{
		/** @brief Position in the order list */
		std::int32_t Order;
		/** @brief Row of the current pattern */
		std::int32_t Row;
		/** @brief Ticks per row */
		std::int32_t Speed;
		/** @brief Tempo, usually in beats per minute */
		std::int32_t Tempo;
	};

	/**
		@brief Interface for an audio reader
		
		Decodes audio samples on demand from a file opened by an @ref IAudioLoader. Stream players
		call @ref Read() repeatedly to refill their buffer queue.
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
		virtual std::int32_t Read(void* buffer, std::int32_t bufferSize) const = 0;

		/** @brief Rewinds the reader back to the beginning */
		virtual void Rewind() const = 0;

		/** @brief Enables native stream looping if the format supports it */
		virtual void SetLooping(bool value) { }

		/**
		 * @{ @name Tracker modules
		 *
		 * Supported only by readers of tracker modules, the others return `false`. Safe to call from any thread while
		 * another one reads, changes are applied by @ref Read() before the frames it decodes next.
		 */

		/** @brief Returns the number of frames decoded so far, it keeps counting across rewinds */
		virtual std::int64_t GetDecodedFrames() const { return 0; }
		/** @brief Returns the state of the module at a frame already decoded, see @ref GetDecodedFrames() */
		virtual bool GetModuleStateAt(std::int64_t frame, AudioModuleState& state) const { return false; }
		/** @brief Returns `true` if @ref SetModulePosition() is supported */
		virtual bool CanSetModulePosition() const { return false; }
		/** @brief Jumps to a row of a position in the order list */
		virtual bool SetModulePosition(std::int32_t order, std::int32_t row) { return false; }
		/** @brief Sets the ticks per row until the module changes them */
		virtual bool SetModuleSpeed(std::int32_t speed) { return false; }
		/** @brief Sets the tempo until the module changes it */
		virtual bool SetModuleTempo(std::int32_t tempo) { return false; }
		/** @brief Changes the volume of a channel gradually, from its current one to @p volume in `0.0` to `1.0` */
		virtual bool SlideModuleChannelVolume(std::int32_t channel, float volume, std::int32_t milliseconds) { return false; }

		/** @} */
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	// Fallback reader used when the audio file extension is not recognized
	class InvalidAudioReader : IAudioReader
	{
	public:
		inline std::int32_t Read(void* buffer, std::int32_t bufferSize) const override { return 0; }
		inline void Rewind() const override { };
	};
#endif
}
