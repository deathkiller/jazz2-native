#pragma once

#include <memory>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	class IAudioReader;
	class IAudioLoader;

	/// OpenAL audio stream
	class AudioStream
	{
		friend class AudioStreamPlayer;

	public:
		~AudioStream();

		/// Returns the OpenAL id of the currently playing buffer, or 0 if not
		inline std::uint32_t bufferId() const {
			return currentBufferId_;
		}

		/// Returns the number of bytes per sample
		inline std::int32_t bytesPerSample() const {
			return bytesPerSample_;
		}
		/// Returns the number of audio channels
		inline std::int32_t numChannels() const {
			return numChannels_;
		}
		/// Returns the samples frequency
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

		/// Returns the size of the loaded buffer in bytes
		inline std::int32_t bufferSize() const {
			return (numSamples_ == -1 ? -1 : (numSamples_ * numChannels_ * bytesPerSample_));
		}

		/// Returns the number of samples in the streaming buffer
		std::int32_t numStreamSamples() const;
		/// Returns the size of the streaming buffer in bytes
		inline std::int32_t streamBufferSize() const {
			return BufferSize;
		}

		/// Enqueues new buffers and unqueues processed ones
		bool enqueue(std::uint32_t source, bool looping);
		/// Unqueues any left buffer and rewinds the loader
		void stop(std::uint32_t source);

		/// Queries the looping property of the stream
		inline bool isLooping() const {
			return isLooping_;
		}
		/// Sets stream looping property
		void setLooping(bool value);

	private:
		/// Number of buffers for streaming
		static const std::int32_t NumBuffers = 3;
		/// OpenAL buffer queue for streaming
		SmallVector<std::uint32_t, NumBuffers> buffersIds_;
		/// Index of the next available OpenAL buffer
		std::int32_t nextAvailableBufferIndex_;

		/// Size in bytes of each streaming buffer
		static const std::int32_t BufferSize = 16 * 1024;
		/// Memory buffer to feed OpenAL ones
		std::unique_ptr<char[]> memBuffer_;

		/// OpenAL id of the currently playing buffer, or 0 if not
		std::uint32_t currentBufferId_;

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

		bool isLooping_;

		/// OpenAL channel format enumeration
		std::int32_t format_;
		/// The associated reader to continuosly stream decoded data
		std::unique_ptr<IAudioReader> audioReader_;

		/// Default constructor
		AudioStream();
		/// Constructor creating an audio stream from an audio file
		explicit AudioStream(StringView filename);

		AudioStream(AudioStream&&);
		AudioStream& operator=(AudioStream&&);
		AudioStream(const AudioStream&) = delete;
		AudioStream& operator=(const AudioStream&) = delete;

		bool loadFromFile(StringView filename);

		void createReader(IAudioLoader& audioLoader);


	};
}
