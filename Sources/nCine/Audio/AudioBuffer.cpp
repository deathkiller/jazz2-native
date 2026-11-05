#include "AudioBuffer.h"
#if !defined(NCINE_HAS_NATIVE_AUDIO)
#	include "IAudioLoader.h"
#endif
#if defined(WITH_AUDIO)
#	include "AudioBufferPlayer.h"
#	include "../ServiceLocator.h"
#endif
#include "../../Main.h"

#include <Containers/String.h>

namespace nCine
{
#if defined(WITH_AUDIO) && !defined(NCINE_HAS_NATIVE_AUDIO)
	namespace
	{
		AudioBuffer::Format GetBufferFormat(std::int32_t bytesPerSample, std::int32_t numChannels)
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
		: Object(ObjectType::AudioBuffer), _bufferId(0), _bytesPerSample(0), _numChannels(0), _frequency(0),
			_numSamples(0), _duration(0.0f)
	{
#if defined(WITH_AUDIO)
		_bufferId = theServiceLocator().GetAudioDevice().CreateBuffer(IAudioDevice::BufferUsage::Static);
		if DEATH_UNLIKELY(_bufferId == 0) {
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
		const bool hasLoaded = LoadFromFile(filename);
		if (!hasLoaded) {
			LOGE("Audio file \"{}\" cannot be loaded", filename);
		}
#endif
	}

#if !defined(NCINE_HAS_NATIVE_AUDIO)
	AudioBuffer::AudioBuffer(std::unique_ptr<Death::IO::Stream> fileHandle, StringView filename)
		: AudioBuffer()
	{
#	if defined(WITH_AUDIO)
		const bool hasLoaded = LoadFromStream(Death::move(fileHandle), filename);
		if (!hasLoaded) {
			LOGE("Audio file \"{}\" cannot be loaded", filename);
		}
#	endif
	}
#endif

	AudioBuffer::~AudioBuffer()
	{
#if defined(WITH_AUDIO)
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		// Moved out objects have their buffer id set to zero
		if (_bufferId != 0) {
			// Stop and detach players still using this buffer, otherwise the backend can't destroy it.
			// Iterating backwards because a stopped player unregisters itself
			for (std::uint32_t i = device.GetPlayerCount(); i > 0; i--) {
				IAudioPlayer* player = device.GetPlayer(i - 1);
				if (player != nullptr && player->type() == AudioBufferPlayer::sType()) {
					AudioBufferPlayer* bufferPlayer = static_cast<AudioBufferPlayer*>(player);
					if (bufferPlayer->GetAudioBuffer() == this) {
						bufferPlayer->SetAudioBuffer(nullptr);
					}
				}
			}
		}

		device.DeleteBuffer(_bufferId);
#endif
	}

	AudioBuffer::AudioBuffer(AudioBuffer&& other) noexcept
		: Object(Death::move(other)), _bufferId(other._bufferId), _bytesPerSample(other._bytesPerSample), _numChannels(other._numChannels),
			_frequency(other._frequency), _numSamples(other._numSamples), _duration(other._duration)
	{
		other._bufferId = 0;
	}

	AudioBuffer& AudioBuffer::operator=(AudioBuffer&& other) noexcept
	{
		Object::operator=(Death::move(other));

		_bufferId = other._bufferId;
		_bytesPerSample = other._bytesPerSample;
		_numChannels = other._numChannels;
		_frequency = other._frequency;
		_numSamples = other._numSamples;
		_duration = other._duration;

		other._bufferId = 0;
		return *this;
	}

#if !defined(NCINE_HAS_NATIVE_AUDIO)
	void AudioBuffer::Init(Format format, std::int32_t frequency)
	{
#	if defined(WITH_AUDIO)
		switch (format) {
			case Format::Mono8:
				_bytesPerSample = 1;
				_numChannels = 1;
				break;
			case Format::Stereo8:
				_bytesPerSample = 1;
				_numChannels = 2;
				break;
			case Format::Mono16:
				_bytesPerSample = 2;
				_numChannels = 1;
				break;
			case Format::Stereo16:
				_bytesPerSample = 2;
				_numChannels = 2;
				break;
		}
		_frequency = frequency;

		LoadFromSamples(nullptr, 0);
#	endif
	}
#endif

	/*bool AudioBuffer::loadFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
#if defined(WITH_AUDIO)
		std::unique_ptr<IAudioLoader> audioLoader = IAudioLoader::createFromMemory(bufferPtr, bufferSize);
		if (audioLoader->HasLoaded()) {
			return Load(*audioLoader.get());
		}
#endif
		return false;
	}*/

	bool AudioBuffer::LoadFromFile(StringView filename)
	{
#if defined(WITH_AUDIO)
#	if defined(NCINE_HAS_NATIVE_AUDIO)
		// The device plays the file by itself (streaming it from storage), no decoded copy is kept at all
		IAudioDevice::NativeAudioInfo info;
		if (theServiceLocator().GetAudioDevice().LoadNativeBuffer(_bufferId, filename, info)) {
			_bytesPerSample = info.BytesPerSample;
			_numChannels = info.NumChannels;
			_frequency = info.Frequency;
			_numSamples = info.NumSamples;
			_duration = (_frequency > 0 && _numSamples > 0 ? float(_numSamples) / _frequency : 0.0f);
			return true;
		}
#	else
		std::unique_ptr<IAudioLoader> audioLoader = IAudioLoader::CreateFromFile(filename);
		if (audioLoader->HasLoaded()) {
			return Load(*audioLoader);
		}
#	endif
#endif
		return false;
	}

#if !defined(NCINE_HAS_NATIVE_AUDIO)
	bool AudioBuffer::LoadFromStream(std::unique_ptr<Death::IO::Stream> fileHandle, StringView filename)
	{
#	if defined(WITH_AUDIO)
		std::unique_ptr<IAudioLoader> audioLoader = IAudioLoader::CreateFromStream(Death::move(fileHandle), filename);
		if (audioLoader->HasLoaded()) {
			return Load(*audioLoader);
		}
#	endif
		return false;
	}

	bool AudioBuffer::LoadFromSamples(const unsigned char* bufferPtr, std::int32_t bufferSize)
	{
#	if defined(WITH_AUDIO)
		if (_bytesPerSample != 0 && _numChannels != 0 && _frequency != 0) {
			if (bufferSize % (_bytesPerSample * _numChannels) != 0) {
				LOGW("Buffer size is incompatible with format");
			}
			const Format format = GetBufferFormat(_bytesPerSample, _numChannels);
			const bool uploaded = theServiceLocator().GetAudioDevice().UploadBuffer(_bufferId, format, bufferPtr, bufferSize, _frequency);
			DEATH_ASSERT(uploaded, "Cannot upload samples to audio buffer", false);

			_numSamples = bufferSize / (_numChannels * _bytesPerSample);
			_duration = float(_numSamples) / _frequency;

			return uploaded;
		}
#	endif
		return false;
	}

	bool AudioBuffer::Load(IAudioLoader& audioLoader)
	{
#	if defined(WITH_AUDIO)
		DEATH_ASSERT(audioLoader.GetBytesPerSample() == 1 || audioLoader.GetBytesPerSample() == 2,
		    ("Unsupported number of bytes per sample: {}", audioLoader.GetBytesPerSample()), false);
		DEATH_ASSERT(audioLoader.GetChannelCount() == 1 || audioLoader.GetChannelCount() == 2,
		    ("Unsupported number of channels: {}", audioLoader.GetChannelCount()), false);

		_bytesPerSample = audioLoader.GetBytesPerSample();
		_numChannels = audioLoader.GetChannelCount();
		_frequency = audioLoader.GetFrequency();

		// Buffer size calculated as samples * channels * bytes per samples
		const std::int32_t bufferSize = std::int32_t(audioLoader.GetBufferSize());
		// No-throw, a large sound may not fit into a fragmented heap mid-level and stays silent then
		std::unique_ptr<unsigned char[]> buffer(new (std::nothrow) unsigned char[bufferSize]);
		if (buffer == nullptr) {
			LOGE("Cannot decode audio buffer: out of memory for {} bytes of samples", bufferSize);
			return false;
		}

		std::unique_ptr<IAudioReader> audioReader = audioLoader.CreateReader();
		// The decoder can produce less data than the loader promised, upload only what was actually read
		const std::int32_t bytesRead = audioReader->Read(buffer.get(), bufferSize);

		return LoadFromSamples(buffer.get(), bytesRead);
#	else
		return false;
#	endif
	}
#endif
}
