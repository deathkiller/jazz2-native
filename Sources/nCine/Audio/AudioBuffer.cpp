#if defined(WITH_AUDIO)
#	define NCINE_INCLUDE_OPENAL
#	include "../CommonHeaders.h"
#endif

#include "AudioBuffer.h"
#include "IAudioLoader.h"
#include "../../Main.h"

#include <Containers/String.h>

namespace nCine
{
#if defined(WITH_AUDIO)
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
#endif

	AudioBuffer::AudioBuffer()
		: Object(ObjectType::AudioBuffer), bufferId_(0), bytesPerSample_(0), numChannels_(0), frequency_(0),
			numSamples_(0), duration_(0.0f)
	{
#if defined(WITH_AUDIO)
		alGetError();
		alGenBuffers(1, &bufferId_);
		const ALenum error = alGetError();
		if DEATH_UNLIKELY(error != AL_NO_ERROR) {
			LOGW("alGenBuffers() failed with error 0x%x", error);
		}
#endif
	}

	/*AudioBuffer::AudioBuffer(const unsigned char* bufferPtr, unsigned long int bufferSize)
		: AudioBuffer()
	{
#if defined(WITH_AUDIO)
		const bool hasLoaded = loadFromMemory(bufferPtr, bufferSize);
		if (!hasLoaded) {
			LOGE("Audio buffer cannot be loaded");
		}
#endif
	}*/

	AudioBuffer::AudioBuffer(StringView filename)
		: AudioBuffer()
	{
#if defined(WITH_AUDIO)
		const bool hasLoaded = loadFromFile(filename);
		if (!hasLoaded) {
			LOGE("Audio file \"%s\" cannot be loaded", String::nullTerminatedView(filename).data());
		}
#endif
	}

	AudioBuffer::AudioBuffer(std::unique_ptr<Death::IO::Stream> fileHandle, StringView filename)
		: AudioBuffer()
	{
#if defined(WITH_AUDIO)
		const bool hasLoaded = loadFromStream(std::move(fileHandle), filename);
		if (!hasLoaded) {
			LOGE("Audio file \"%s\" cannot be loaded", String::nullTerminatedView(filename).data());
		}
#endif
	}

	AudioBuffer::~AudioBuffer()
	{
#if defined(WITH_AUDIO)
		// Moved out objects have their buffer id set to zero
		alDeleteBuffers(1, &bufferId_);
#endif
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

	void AudioBuffer::init(Format format, std::int32_t frequency)
	{
#if defined(WITH_AUDIO)
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
#endif
	}

	/*bool AudioBuffer::loadFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
#if defined(WITH_AUDIO)
		std::unique_ptr<IAudioLoader> audioLoader = IAudioLoader::createFromMemory(bufferPtr, bufferSize);
		if (audioLoader->hasLoaded()) {
			return load(*audioLoader.get());
		}
#endif
		return false;
	}*/

	bool AudioBuffer::loadFromFile(StringView filename)
	{
#if defined(WITH_AUDIO)
		std::unique_ptr<IAudioLoader> audioLoader = IAudioLoader::createFromFile(filename);
		if (audioLoader->hasLoaded()) {
			return load(*audioLoader);
		}
#endif
		return false;
	}

	bool AudioBuffer::loadFromStream(std::unique_ptr<Death::IO::Stream> fileHandle, StringView filename)
	{
#if defined(WITH_AUDIO)
		std::unique_ptr<IAudioLoader> audioLoader = IAudioLoader::createFromStream(std::move(fileHandle), filename);
		if (audioLoader->hasLoaded()) {
			return load(*audioLoader);
		}
#endif
		return false;
	}

	bool AudioBuffer::loadFromSamples(const unsigned char* bufferPtr, std::int32_t bufferSize)
	{
#if defined(WITH_AUDIO)
		if (bytesPerSample_ != 0 && numChannels_ != 0 && frequency_ != 0) {
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
#endif
		return false;
	}

	bool AudioBuffer::load(IAudioLoader& audioLoader)
	{
#if defined(WITH_AUDIO)
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
#else
		return false;
#endif
	}
}
