#pragma once

#include <memory>

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	class IAudioReader;
	class IAudioLoader;

	/// Audio stream class
	class AudioStream
	{
	public:
		~AudioStream();

		/// Returns the OpenAL id of the currently playing buffer, or 0 if not
		inline unsigned int bufferId() const {
			return currentBufferId_;
		}

		/// Returns the number of bytes per sample
		inline int bytesPerSample() const {
			return bytesPerSample_;
		}
		/// Returns the number of audio channels
		inline int numChannels() const {
			return numChannels_;
		}
		/// Returns the samples frequency
		inline int frequency() const {
			return frequency_;
		}

		/// Returns number of samples
		inline unsigned long int numSamples() const {
			return numSamples_;
		}
		/// Returns the duration in seconds
		inline float duration() const {
			return duration_;
		}

		/// Returns the size of the loaded buffer in bytes
		inline unsigned long bufferSize() const {
			return (numSamples_ == UINT32_MAX ? UINT32_MAX : (numSamples_ * numChannels_ * bytesPerSample_));
		}

		/// Returns the number of samples in the streaming buffer
		unsigned long int numStreamSamples() const;
		/// Returns the size of the streaming buffer in bytes
		inline int streamBufferSize() const {
			return BufferSize;
		}

		/// Enqueues new buffers and unqueues processed ones
		bool enqueue(unsigned int source, bool looping);
		/// Unqueues any left buffer and rewinds the loader
		void stop(unsigned int source);

		/// Queries the looping property of the stream
		inline bool isLooping() const {
			return isLooping_;
		}
		/// Sets stream looping property
		void setLooping(bool value);

	private:
		/// Number of buffers for streaming
		static const int NumBuffers = 3;
		/// OpenAL buffer queue for streaming
		SmallVector<unsigned int, NumBuffers> buffersIds_;
		/// Index of the next available OpenAL buffer
		int nextAvailableBufferIndex_;

		/// Size in bytes of each streaming buffer
		static const int BufferSize = 16 * 1024;
		/// Memory buffer to feed OpenAL ones
		std::unique_ptr<char[]> memBuffer_;

		/// OpenAL id of the currently playing buffer, or 0 if not
		unsigned int currentBufferId_;

		/// Number of bytes per sample
		int bytesPerSample_;
		/// Number of channels
		int numChannels_;
		/// Samples frequency
		int frequency_;

		/// Number of samples
		unsigned long int numSamples_;
		/// Duration in seconds
		float duration_;

		bool isLooping_;

		/// OpenAL channel format enumeration
		int format_;
		/// The associated reader to continuosly stream decoded data
		std::unique_ptr<IAudioReader> audioReader_;

		/// Default constructor
		AudioStream();
		/// Constructor creating an audio stream from a named memory buffer
		//AudioStream(const unsigned char* bufferPtr, unsigned long int bufferSize);
		/// Constructor creating an audio stream from an audio file
		explicit AudioStream(const StringView& filename);

		/// Default move constructor
		AudioStream(AudioStream&&);
		/// Default move assignment operator
		AudioStream& operator=(AudioStream&&);

		//bool loadFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize);
		bool loadFromFile(const StringView& filename);

		void createReader(IAudioLoader& audioLoader);

		/// Deleted copy constructor
		AudioStream(const AudioStream&) = delete;
		/// Deleted assignment operator
		AudioStream& operator=(const AudioStream&) = delete;

		friend class AudioStreamPlayer;
	};
}
