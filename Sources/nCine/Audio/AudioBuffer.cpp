#define NCINE_INCLUDE_OPENAL
#include "../CommonHeaders.h"

#include "AudioBuffer.h"
#include "IAudioLoader.h"
#include "../../Common.h"

namespace nCine
{
	namespace
	{
		ALenum alFormat(int bytesPerSample, int numChannels)
		{
			ALenum format = AL_FORMAT_MONO8;
			if (bytesPerSample == 1 && numChannels == 2) {
				format = AL_FORMAT_STEREO8;
			} else if (bytesPerSample == 2 && numChannels == 1) {
				format = AL_FORMAT_MONO16;
			} else if (bytesPerSample == 2 && numChannels == 2) {
				format = AL_FORMAT_STEREO16;
			}
			return format;
		}
	}

	AudioBuffer::AudioBuffer()
		: Object(ObjectType::AudioBuffer), bufferId_(0), bytesPerSample_(0), numChannels_(0), frequency_(0), numSamples_(0), duration_(0.0f)
	{
		alGetError();
		alGenBuffers(1, &bufferId_);
		const ALenum error = alGetError();
		FATAL_ASSERT_MSG(error == AL_NO_ERROR, "alGenBuffers() failed with error 0x%x", error);
	}

	/*AudioBuffer::AudioBuffer(const unsigned char* bufferPtr, unsigned long int bufferSize)
		: AudioBuffer()
	{
		const bool hasLoaded = loadFromMemory(bufferPtr, bufferSize);
		if (!hasLoaded) {
			LOGE("Audio buffer cannot be loaded");
		}
	}*/

	AudioBuffer::AudioBuffer(const StringView filename)
		: AudioBuffer()
	{
		const bool hasLoaded = loadFromFile(filename);
		if (!hasLoaded) {
			LOGE("Audio file \"%s\" cannot be loaded", filename.data());
		}
	}

	AudioBuffer::AudioBuffer(std::unique_ptr<Death::IO::Stream> fileHandle, const StringView filename)
		: AudioBuffer()
	{
		const bool hasLoaded = loadFromStream(std::move(fileHandle), filename);
		if (!hasLoaded) {
			LOGE("Audio file \"%s\" cannot be loaded", filename.data());
		}
	}

	AudioBuffer::~AudioBuffer()
	{
		// Moved out objects have their buffer id set to zero
		alDeleteBuffers(1, &bufferId_);
	}

	AudioBuffer::AudioBuffer(AudioBuffer&& other) noexcept
		: Object(std::move(other)), bufferId_(other.bufferId_), bytesPerSample_(other.bytesPerSample_), numChannels_(other.numChannels_),
			frequency_(other.frequency_), numSamples_(other.numSamples_), duration_(other.duration_)
	{
		other.bufferId_ = 0;
	}

	AudioBuffer& AudioBuffer::operator=(AudioBuffer&& other) noexcept
	{
		Object::operator=(std::move(other));

		bufferId_ = other.bufferId_;
		bytesPerSample_ = other.bytesPerSample_;
		numChannels_ = other.numChannels_;
		frequency_ = other.frequency_;
		numSamples_ = other.numSamples_;
		duration_ = other.duration_;

		other.bufferId_ = 0;
		return *this;
	}

	void AudioBuffer::init(Format format, int frequency)
	{
		switch (format) {
			case Format::Mono8:
				bytesPerSample_ = 1;
				numChannels_ = 1;
				break;
			case Format::Stereo8:
				bytesPerSample_ = 1;
				numChannels_ = 2;
				break;
			case Format::Mono16:
				bytesPerSample_ = 2;
				numChannels_ = 1;
				break;
			case Format::Stereo16:
				bytesPerSample_ = 2;
				numChannels_ = 2;
				break;
		}
		frequency_ = frequency;

		loadFromSamples(nullptr, 0);
	}

	/*bool AudioBuffer::loadFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		std::unique_ptr<IAudioLoader> audioLoader = IAudioLoader::createFromMemory(bufferPtr, bufferSize);
		if (!audioLoader->hasLoaded()) {
			return false;
		}

		const bool samplesHaveLoaded = load(*audioLoader.get());
		return samplesHaveLoaded;
	}*/

	bool AudioBuffer::loadFromFile(const StringView filename)
	{
		std::unique_ptr<IAudioLoader> audioLoader = IAudioLoader::createFromFile(filename);
		if (!audioLoader->hasLoaded()) {
			return false;
		}

		const bool samplesHaveLoaded = load(*audioLoader);
		return samplesHaveLoaded;
	}

	bool AudioBuffer::loadFromStream(std::unique_ptr<Death::IO::Stream> fileHandle, const StringView filename)
	{
		std::unique_ptr<IAudioLoader> audioLoader = IAudioLoader::createFromStream(std::move(fileHandle), filename);
		if (!audioLoader->hasLoaded()) {
			return false;
		}

		const bool samplesHaveLoaded = load(*audioLoader);
		return samplesHaveLoaded;
	}

	bool AudioBuffer::loadFromSamples(const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		if (bytesPerSample_ == 0 || numChannels_ == 0 || frequency_ == 0) {
			return false;
		}

		if (bufferSize % (bytesPerSample_ * numChannels_) != 0) {
			LOGW("Buffer size is incompatible with format");
		}
		const ALenum format = alFormat(bytesPerSample_, numChannels_);

		alGetError();

		// On iOS `alBufferDataStatic()` could be used instead
		alBufferData(bufferId_, format, bufferPtr, bufferSize, frequency_);
		const ALenum error = alGetError();
		RETURNF_ASSERT_MSG(error == AL_NO_ERROR, "alBufferData() failed with error 0x%x", error);

		numSamples_ = bufferSize / (numChannels_ * bytesPerSample_);
		duration_ = float(numSamples_) / frequency_;

		return (error == AL_NO_ERROR);
	}

	bool AudioBuffer::load(IAudioLoader& audioLoader)
	{
		RETURNF_ASSERT_MSG(audioLoader.bytesPerSample() == 1 || audioLoader.bytesPerSample() == 2,
		                     "Unsupported number of bytes per sample: %d", audioLoader.bytesPerSample());
		RETURNF_ASSERT_MSG(audioLoader.numChannels() == 1 || audioLoader.numChannels() == 2,
		                     "Unsupported number of channels: %d", audioLoader.numChannels());

		bytesPerSample_ = audioLoader.bytesPerSample();
		numChannels_ = audioLoader.numChannels();
		frequency_ = audioLoader.frequency();

		// Buffer size calculated as samples * channels * bytes per samples
		const unsigned long int bufferSize = audioLoader.bufferSize();
		std::unique_ptr<unsigned char[]> buffer = std::make_unique<unsigned char[]>(bufferSize);

		std::unique_ptr<IAudioReader> audioReader = audioLoader.createReader();
		audioReader->read(buffer.get(), bufferSize);

		return loadFromSamples(buffer.get(), bufferSize);
	}
}
