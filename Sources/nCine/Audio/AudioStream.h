#pragma once

#include <memory>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	class IAudioReader;
	class IAudioLoader;

	/**
		@brief Streams audio decoded on the fly into a rotating set of OpenAL buffers
		
		Used for long sounds such as music, where decoding the whole file into memory would be
		wasteful. Owned by @ref AudioStreamPlayer, which feeds it through @ref enqueue().
	*/
	class AudioStream
	{
		friend class AudioStreamPlayer;

	public:
		~AudioStream();

		/** @brief Returns the OpenAL id of the currently playing buffer, or `0` if none */
		inline std::uint32_t bufferId() const {
			return currentBufferId_;
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

		/** @brief Returns the total number of samples, or `-1` if unknown */
		inline std::int32_t numSamples() const {
			return numSamples_;
		}
		/** @brief Returns the duration in seconds */
		inline float duration() const {
			return duration_;
		}

		/** @brief Returns the total decoded size in bytes, or `-1` if unknown */
		inline std::int32_t bufferSize() const {
			return (numSamples_ == -1 ? -1 : (numSamples_ * numChannels_ * bytesPerSample_));
		}

		/** @brief Returns the number of samples held by a single streaming buffer */
		std::int32_t numStreamSamples() const;
		/** @brief Returns the size of a single streaming buffer in bytes */
		inline std::int32_t streamBufferSize() const {
			return BufferSize;
		}

		/**
		 * @brief Decodes and enqueues new buffers and unqueues processed ones
		 *
		 * @return `false` once the stream has been entirely decoded and played
		 */
		bool enqueue(std::uint32_t source, bool looping);
		/** @brief Unqueues any remaining buffers and rewinds the reader */
		void stop(std::uint32_t source);

		/** @brief Returns `true` if the stream is looping */
		inline bool isLooping() const {
			return isLooping_;
		}
		/** @brief Sets whether the stream should loop */
		void setLooping(bool value);

	private:
		/** @brief Number of buffers used for streaming */
		static const std::int32_t NumBuffers = 3;
		/** @brief OpenAL buffer queue used for streaming */
		SmallVector<std::uint32_t, NumBuffers> buffersIds_;
		/** @brief Index of the next available OpenAL buffer */
		std::int32_t nextAvailableBufferIndex_;

		/** @brief Size in bytes of each streaming buffer */
		static const std::int32_t BufferSize = 16 * 1024;
		/** @brief Intermediate memory buffer feeding the OpenAL buffers */
		std::unique_ptr<char[]> memBuffer_;

		/** @brief OpenAL id of the currently playing buffer, or 0 if none */
		std::uint32_t currentBufferId_;

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

		/** @brief Whether the stream loops */
		bool isLooping_;

		/** @brief OpenAL channel format enumeration */
		std::int32_t format_;
		/** @brief Reader that continuously streams decoded data */
		std::unique_ptr<IAudioReader> audioReader_;

		// Private constructors called only by AudioStreamPlayer
		AudioStream();
		explicit AudioStream(StringView filename);

		AudioStream(AudioStream&&);
		AudioStream& operator=(AudioStream&&);
		AudioStream(const AudioStream&) = delete;
		AudioStream& operator=(const AudioStream&) = delete;

		bool loadFromFile(StringView filename);

		void createReader(IAudioLoader& audioLoader);

	};
}
