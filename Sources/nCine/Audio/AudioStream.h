#pragma once

#include "IAudioDevice.h"

#include <memory>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	class IAudioReader;
	class IAudioLoader;

	/**
		@brief Streams audio decoded on the fly into a rotating set of backend buffers

		Used for long sounds such as music, where decoding the whole file into memory would be
		wasteful. Owned by @ref AudioStreamPlayer, which feeds it through @ref enqueue().
	*/
	class AudioStream
	{
		friend class AudioStreamPlayer;

	public:
		~AudioStream();

		/** @brief Returns the backend id of the currently playing buffer, or `0` if none */
		inline std::uint32_t bufferId() const {
			return _currentBufferId;
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

		/** @brief Returns the total number of samples, or `-1` if unknown */
		inline std::int32_t numSamples() const {
			return _numSamples;
		}
		/** @brief Returns the duration in seconds */
		inline float duration() const {
			return _duration;
		}

		/** @brief Returns the total decoded size in bytes, or `-1` if unknown */
		inline std::int32_t bufferSize() const {
			return (_numSamples == -1 ? -1 : (_numSamples * _numChannels * _bytesPerSample));
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
			return _isLooping;
		}
		/** @brief Sets whether the stream should loop */
		void setLooping(bool value);

	private:
		/** @brief Number of buffers used for streaming */
		static const std::int32_t NumBuffers = 3;
		/** @brief Backend buffer queue used for streaming */
		SmallVector<std::uint32_t, NumBuffers> _buffersIds;
		/** @brief Index of the next available buffer, which is also the number of buffers currently queued */
		std::int32_t _nextAvailableBufferIndex;

		/**
		 * @brief Returns the buffers the backend has finished playing to the free list
		 *
		 * Bounded by what this object can actually account for, which the two call sites used to take on
		 * trust from the backend --- see the comment on the definition for what that cost.
		 *
		 * @return Number of buffers returned
		 */
		std::int32_t unqueueProcessedBuffers(IAudioDevice& device, std::uint32_t source);

		/** @brief Size in bytes of each streaming buffer */
		// One size for every platform, because what makes a chunk the right size is not how fast the machine
		// is but how it is consumed: only ONE chunk is ever decoded ahead, so a chunk has to hold more audio
		// than a frame plays or the queue cannot build a lead and the main thread ends up blocked waiting on
		// the decoding thread for every buffer. The PSP used to take a quarter of this on the theory that a
		// smaller synchronous decode spreads the cost - measured, it did the opposite: at 4 KB a chunk held
		// 46 ms and took ~43 ms of wall time to produce, so production and consumption ran level with no
		// margin, the queue sat permanently at one buffer, and the frame paid 3-6 ms of pure waiting. The
		// larger chunk also costs less per byte: the same decode measured 3.0 ms of CPU uncontended at load
		// against 11.1 ms in a running frame, nearly all of it the renderer evicting the mixer's samples from
		// a 16 KB D-cache, and a chunk four times the size pays that cold start a quarter as often.
		static const std::int32_t BufferSize = 16 * 1024;
		/** @brief Reusable decode request holding the intermediate buffer, executed ahead of time on the decoding thread when available */
		std::shared_ptr<StreamDecodeRequest> _decodeRequest;

		/** @brief Backend id of the currently playing buffer, or 0 if none */
		std::uint32_t _currentBufferId;

		/** @brief Whether the last chunk could be handed to a decoding thread, so this one need not decode it */
		bool _asyncDecodeAvailable;

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

		/** @brief Whether the stream loops */
		bool _isLooping;

		/** @brief Sample format of the decoded data */
		IAudioDevice::BufferFormat _format;
		/** @brief Reader that continuously streams decoded data, shared with @ref _decodeRequest */
		std::shared_ptr<IAudioReader> _audioReader;

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
