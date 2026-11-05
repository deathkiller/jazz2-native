#pragma once

#include "IAudioDevice.h"
#include "IAudioReader.h"
#include "../Base/TimeStamp.h"

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
		
		Used for long sounds such as music, owned by @ref AudioStreamPlayer. Where `NCINE_HAS_NATIVE_AUDIO`
		is defined, the device plays the file by itself (see @ref IAudioDevice::OpenNativeStream()).
	*/
	class AudioStream
	{
		friend class AudioStreamPlayer;

	public:
		~AudioStream();

		/** @brief Returns the backend id of the currently playing buffer, or `0` if none */
		inline std::uint32_t GetBufferId() const {
#if defined(NCINE_HAS_NATIVE_AUDIO)
			// A native stream plays through no buffer of the engine's
			return 0;
#else
			return _currentBufferId;
#endif
		}

		/** @brief Returns the number of bytes per sample */
		inline std::int32_t GetBytesPerSample() const {
			return _bytesPerSample;
		}
		/** @brief Returns the number of audio channels */
		inline std::int32_t GetChannelCount() const {
			return _numChannels;
		}
		/** @brief Returns the sample frequency */
		inline std::int32_t GetFrequency() const {
			return _frequency;
		}

		/** @brief Returns the total number of samples, or `-1` if unknown */
		inline std::int32_t GetSampleCount() const {
			return _numSamples;
		}
		/** @brief Returns the duration in seconds */
		inline float GetDuration() const {
			return _duration;
		}

		/** @brief Returns the total decoded size in bytes, or `-1` if unknown */
		inline std::int32_t GetBufferSize() const {
			return (_numSamples == -1 ? -1 : (_numSamples * _numChannels * _bytesPerSample));
		}

#if !defined(NCINE_HAS_NATIVE_AUDIO) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Returns the number of samples held by a single streaming buffer */
		std::int32_t GetStreamSampleCount() const;
		/** @brief Returns the size of a single streaming buffer in bytes */
		inline std::int32_t GetStreamBufferSize() const {
			return BufferSize;
		}
#endif

		/**
		 * @brief Decodes and enqueues new buffers and unqueues processed ones
		 *
		 * @return `false` once the stream has been entirely decoded and played
		 */
		bool Enqueue(std::uint32_t source, bool looping);
		/** @brief Unqueues any remaining buffers and rewinds the reader */
		void Stop(std::uint32_t source);

		/** @brief Returns `true` if the stream is looping */
		inline bool IsLooping() const {
			return _isLooping;
		}
		/** @brief Sets whether the stream should loop */
		void SetLooping(bool value);

		/**
		 * @brief Returns the state of a tracker module at the position being heard, `false` if the stream isn't a module
		 *
		 * @partialsupport Not every decoder supports it, always returns `false` where `NCINE_HAS_NATIVE_AUDIO` is defined.
		 */
		bool GetModuleState(AudioModuleState& state) const;
		/**
		 * @brief Jumps to a row of a position in the order list of a tracker module
		 *
		 * The queued audio is dropped. With @p restart, the queue is filled again from the new position and the source
		 * plays at once, otherwise it stays stopped until @ref Enqueue() is called.
		 *
		 * @partialsupport Not every decoder supports it, has no effect where `NCINE_HAS_NATIVE_AUDIO` is defined.
		 */
		bool SetModulePosition(std::uint32_t source, std::int32_t order, std::int32_t row, bool restart, bool looping);
		/**
		 * @brief Sets the ticks per row of a tracker module until the module changes them
		 *
		 * @partialsupport Not every decoder supports it, has no effect where `NCINE_HAS_NATIVE_AUDIO` is defined.
		 */
		bool SetModuleSpeed(std::int32_t speed);
		/**
		 * @brief Sets the tempo of a tracker module until the module changes it
		 *
		 * @partialsupport Not every decoder supports it, has no effect where `NCINE_HAS_NATIVE_AUDIO` is defined.
		 */
		bool SetModuleTempo(std::int32_t tempo);
		/**
		 * @brief Changes the volume of a channel of a tracker module gradually, @p volume is in `0.0` to `1.0`
		 *
		 * @partialsupport Not every decoder supports it, has no effect where `NCINE_HAS_NATIVE_AUDIO` is defined.
		 */
		bool SlideModuleChannelVolume(std::int32_t channel, float volume, std::int32_t milliseconds);

	private:
#if defined(NCINE_HAS_NATIVE_AUDIO)
		/** @brief Stream the device plays by itself (see @ref IAudioDevice::OpenNativeStream()), closed with the handle */
		struct NativeStreamHandle
		{
			/** @brief Id returned by @ref IAudioDevice::OpenNativeStream(), `0` if none */
			std::uint32_t Id = 0;
			/** @brief Whether the stream has been bound to the source and started */
			bool Started = false;

			NativeStreamHandle() = default;
			explicit NativeStreamHandle(std::uint32_t id) : Id(id) {}
			~NativeStreamHandle();
			NativeStreamHandle(const NativeStreamHandle&) = delete;
			NativeStreamHandle& operator=(const NativeStreamHandle&) = delete;
			// A moved-from stream must not close what it no longer owns; the assignment hands the previous
			// stream to the moved-from object, whose destructor then closes it
			NativeStreamHandle(NativeStreamHandle&& other) noexcept : Id(other.Id), Started(other.Started) { other.Id = 0; }
			NativeStreamHandle& operator=(NativeStreamHandle&& other) noexcept {
				std::uint32_t id = Id; Id = other.Id; other.Id = id;
				Started = other.Started;
				return *this;
			}
		};
		/** @brief Stream being played */
		NativeStreamHandle _nativeStream;
#else
		/** @brief Number of buffers used for streaming */
		static const std::int32_t NumBuffers = 3;
		/** @brief Backend buffer queue used for streaming */
		SmallVector<std::uint32_t, NumBuffers> _buffersIds;
		/** @brief Index of the next available buffer, which is also the number of buffers currently queued */
		std::int32_t _nextAvailableBufferIndex;

		/**
		 * @brief Returns the buffers the backend has finished playing to the free list
		 *
		 * @return Number of buffers returned, never more than this object queued
		 */
		std::int32_t UnqueueProcessedBuffers(IAudioDevice& device, std::uint32_t source);

		/** @brief Size in bytes of each streaming buffer */
		// Same on all platforms: only one chunk is decoded ahead, so it must hold more than a frame plays,
		// otherwise the main thread waits on the decoder for every buffer (4 KB on PSP was measurably slower)
		static const std::int32_t BufferSize = 16 * 1024;
		/** @brief Reusable decode request holding the intermediate buffer, executed ahead of time on the decoding thread when available */
		std::shared_ptr<StreamDecodeRequest> _decodeRequest;

		/** @brief Backend id of the currently playing buffer, or 0 if none */
		std::uint32_t _currentBufferId;

		/** @brief Whether the last chunk could be handed to a decoding thread, so this one need not decode it */
		bool _asyncDecodeAvailable;

		/** @brief Sample format of the decoded data */
		IAudioDevice::BufferFormat _format;
		/** @brief Reader that continuously streams decoded data, shared with @ref _decodeRequest */
		std::shared_ptr<IAudioReader> _audioReader;

		/** @brief Frames of the reader held by a queued buffer, see @ref IAudioReader::GetDecodedFrames() */
		struct QueuedChunk {
			std::int64_t StartFrame;
			std::int32_t FrameCount;
		};
		/** @brief Queued buffers in the order they play, a ring starting at @ref _firstQueuedChunk */
		QueuedChunk _queuedChunks[NumBuffers] = {};
		std::int32_t _firstQueuedChunk = 0;
		std::int32_t _queuedChunkCount = 0;
		/** @brief When the first queued buffer started to play, as far as can be told */
		TimeStamp _firstChunkStartTime;

		void PushQueuedChunk(std::int32_t bytes);
		void PopQueuedChunks(std::int32_t count);
#endif

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

		// Private constructors called only by AudioStreamPlayer
		AudioStream();
		explicit AudioStream(StringView filename);

		AudioStream(AudioStream&&);
		AudioStream& operator=(AudioStream&&);
		AudioStream(const AudioStream&) = delete;
		AudioStream& operator=(const AudioStream&) = delete;

		bool LoadFromFile(StringView filename);

#if !defined(NCINE_HAS_NATIVE_AUDIO)
		void CreateReader(IAudioLoader& audioLoader);
#endif
	};
}
