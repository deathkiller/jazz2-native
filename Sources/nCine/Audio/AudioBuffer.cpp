#include "AudioBuffer.h"
#include "IAudioLoader.h"
#if defined(WITH_AUDIO)
#	include "AudioBufferPlayer.h"
#	include "../ServiceLocator.h"
#endif
#include "../../Main.h"

#include <Containers/String.h>

namespace nCine
{
#if defined(WITH_AUDIO)
	namespace
	{
		AudioBuffer::Format bufferFormat(std::int32_t bytesPerSample, std::int32_t numChannels)
		{
			AudioBuffer::Format format = AudioBuffer::Format::Mono8;
			if (bytesPerSample == 1 && numChannels == 2) {
				format = AudioBuffer::Format::Stereo8;
			} else if (bytesPerSample == 2 && numChannels == 1) {
				format = AudioBuffer::Format::Mono16;
			} else if (bytesPerSample == 2 && numChannels == 2) {
				format = AudioBuffer::Format::Stereo16;
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
		bufferId_ = theServiceLocator().GetAudioDevice().createBuffer(IAudioDevice::BufferUsage::Static);
		if DEATH_UNLIKELY(bufferId_ == 0) {
			LOGW("Cannot create audio buffer");
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
			LOGE("Audio file \"{}\" cannot be loaded", filename);
		}
#endif
	}

	AudioBuffer::AudioBuffer(std::unique_ptr<Death::IO::Stream> fileHandle, StringView filename)
		: AudioBuffer()
	{
#if defined(WITH_AUDIO)
		const bool hasLoaded = loadFromStream(std::move(fileHandle), filename);
		if (!hasLoaded) {
			LOGE("Audio file \"{}\" cannot be loaded", filename);
		}
#endif
	}

	AudioBuffer::~AudioBuffer()
	{
#if defined(WITH_AUDIO)
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		// Moved out objects have their buffer id set to zero
		if (bufferId_ != 0) {
			// Stop any player that still uses this buffer, otherwise the buffer stays attached to its
			// source, the backend refuses to destroy it and the buffer leaks. This also clears the
			// players' pointer to this buffer, which is dangling from now on.
			// Iterating backwards because a stopped player unregisters itself, erasing it from the device
			for (std::uint32_t i = device.numPlayers(); i > 0; i--) {
				IAudioPlayer* player = device.player(i - 1);
				if (player != nullptr && player->type() == AudioBufferPlayer::sType()) {
					AudioBufferPlayer* bufferPlayer = static_cast<AudioBufferPlayer*>(player);
					if (bufferPlayer->audioBuffer() == this) {
						bufferPlayer->setAudioBuffer(nullptr);
					}
				}
			}
		}

		device.deleteBuffer(bufferId_);
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
			const Format format = bufferFormat(bytesPerSample_, numChannels_);
			const bool uploaded = theServiceLocator().GetAudioDevice().uploadBuffer(bufferId_, format, bufferPtr, bufferSize, frequency_);
			DEATH_ASSERT(uploaded, "Cannot upload samples to audio buffer", false);

			numSamples_ = bufferSize / (numChannels_ * bytesPerSample_);
			duration_ = float(numSamples_) / frequency_;

			return uploaded;
		}
#endif
		return false;
	}

	bool AudioBuffer::load(IAudioLoader& audioLoader)
	{
#if defined(WITH_AUDIO)
		DEATH_ASSERT(audioLoader.bytesPerSample() == 1 || audioLoader.bytesPerSample() == 2,
		    ("Unsupported number of bytes per sample: {}", audioLoader.bytesPerSample()), false);
		DEATH_ASSERT(audioLoader.numChannels() == 1 || audioLoader.numChannels() == 2,
		    ("Unsupported number of channels: {}", audioLoader.numChannels()), false);

		bytesPerSample_ = audioLoader.bytesPerSample();
		numChannels_ = audioLoader.numChannels();
		frequency_ = audioLoader.frequency();

		// Buffer size calculated as samples * channels * bytes per samples
		const std::int32_t bufferSize = std::int32_t(audioLoader.bufferSize());
		std::unique_ptr<unsigned char[]> buffer = std::make_unique<unsigned char[]>(bufferSize);

		std::unique_ptr<IAudioReader> audioReader = audioLoader.createReader();
		// The decoder can produce less data than the loader promised, upload only what was actually read
		const std::int32_t bytesRead = audioReader->read(buffer.get(), bufferSize);

		return loadFromSamples(buffer.get(), bytesRead);
#else
		return false;
#endif
	}
}
