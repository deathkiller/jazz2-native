#include "ALAudioDevice.h"
#include "ALDebug.h"
#include "../../AudioBufferPlayer.h"
#include "../../AudioStreamPlayer.h"
#include "../../../ServiceLocator.h"

#include <Containers/StringUtils.h>

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
#	include <Environment.h>
#	include <Utf8.h>
#endif

using namespace Death;
using namespace Death::Containers::Literals;

namespace nCine
{
	namespace
	{
		ALenum alFormat(IAudioDevice::BufferFormat format)
		{
			switch (format) {
				case IAudioDevice::BufferFormat::Stereo8: return AL_FORMAT_STEREO8;
				case IAudioDevice::BufferFormat::Mono16: return AL_FORMAT_MONO16;
				case IAudioDevice::BufferFormat::Stereo16: return AL_FORMAT_STEREO16;
				default: return AL_FORMAT_MONO8;
			}
		}
	}

	ALAudioDevice::ALAudioDevice()
		: _device(nullptr), _context(nullptr), _sources{}, _nativeFreq(44100), _deviceName(nullptr)
#if defined(OPENAL_FILTERS_SUPPORTED)
		, _filters{}
#endif
#if defined(WITH_LIBRETRO)
		, _alcRenderSamplesSOFT(nullptr)
#endif
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		, _alcReopenDeviceSOFT(nullptr), _pEnumerator(nullptr), _lastDeviceChangeTime(0), _shouldRecreate(false)
#endif
	{
		Init();
	}

	void ALAudioDevice::Init()
	{
		LOGD("Initializing OpenAL audio device...");

#if defined(WITH_LIBRETRO)
		// The core hands mixed audio to the frontend instead of playing it itself: an ALC_SOFT_loopback
		// device mixes on demand and retro_run pulls the samples through renderSamples(). The core is
		// always built against OpenAL Soft, which has the extension since v1.14.
		auto alcLoopbackOpenDeviceSOFT = (LPALCLOOPBACKOPENDEVICESOFT)alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT");
		_alcRenderSamplesSOFT = (LPALCRENDERSAMPLESSOFT)alcGetProcAddress(nullptr, "alcRenderSamplesSOFT");
		if (alcLoopbackOpenDeviceSOFT != nullptr && _alcRenderSamplesSOFT != nullptr) {
			_device = alcLoopbackOpenDeviceSOFT(nullptr);
		} else {
			LOGE("The OpenAL library does not support ALC_SOFT_loopback");
		}
#else
		_device = alcOpenDevice(nullptr);
#endif
		DEATH_ASSERT(_device != nullptr, ("alcOpenDevice() failed with error 0x{:x}", alGetError()), );
		_deviceName = alcGetString(_device, ALC_DEVICE_SPECIFIER);

#if defined(WITH_LIBRETRO)
		// A loopback context must be created with its render format: 16-bit interleaved stereo (what
		// retro_audio_sample_batch expects) at the rate advertised in retro_system_av_info
		const ALCint loopbackAttrs[] = {
			ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
			ALC_FORMAT_TYPE_SOFT, ALC_SHORT_SOFT,
			ALC_FREQUENCY, 48000,
			0
		};
		_context = alcCreateContext(_device, loopbackAttrs);
#else
		_context = alcCreateContext(_device, nullptr);
#endif
		if (_context == nullptr) {
			alcCloseDevice(_device);
			LOGE("alcCreateContext() failed with error 0x{:x}", alGetError());
			return;
		}

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		// Try to get native sample rate of default audio device (default is 44100)
		ALCint nativeFreq = 0;
		alcGetIntegerv(_device, ALC_FREQUENCY, 1, &nativeFreq);
		if (nativeFreq >= 44100 && nativeFreq <= 192000) {
			_nativeFreq = nativeFreq;
		}
#endif

		if (!alcMakeContextCurrent(_context)) {
			alcDestroyContext(_context);
			alcCloseDevice(_device);
			LOGE("alcMakeContextCurrent() failed with error 0x{:x}", alGetError());
			return;
		}

		alGetError();
		alGenSources(MaxSources, _sources);
		const ALenum error = alGetError();
		if (error != AL_NO_ERROR) {
			LOGE("alGenSources() failed with error 0x{:x}", error);
		} else {
			// The distance model parameters never change per source, so they are applied once here
			// instead of every time a player takes a source out of the pool
			for (std::int32_t i = 0; i < MaxSources; i++) {
				alSourcef(_sources[i], AL_REFERENCE_DISTANCE, ReferenceDistance);
				alSourcef(_sources[i], AL_MAX_DISTANCE, MaxDistance);
			}

			std::uint32_t sourceIds[MaxSources];
			for (std::int32_t i = 0; i < MaxSources; i++) {
				sourceIds[i] = _sources[i];
			}
			setSourcePool(arrayView(sourceIds, MaxSources));
		}

		alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
		alDopplerFactor(1.0f);
		alSpeedOfSound(360.0f);
		alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
		alListenerf(AL_GAIN, _gain);

#if defined(AL_STOP_SOURCES_ON_DISCONNECT_SOFT) && !defined(DEATH_TARGET_EMSCRIPTEN)
		// Don't stop sources when device is disconnected if supported
		alDisable(AL_STOP_SOURCES_ON_DISCONNECT_SOFT);
#endif

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		// Try to use ALC_SOFT_reopen_device extension to reopen the device, it's a context
		// extension, so it has to be queried with alcGetProcAddress() and the device
		if (alcIsExtensionPresent(_device, "ALC_SOFT_reopen_device")) {
			_alcReopenDeviceSOFT = (LPALCREOPENDEVICESOFT)alcGetProcAddress(_device, "alcReopenDeviceSOFT");
		}
		registerAudioEvents();
#endif

#if defined(DEATH_TRACE)
		// Log OpenAL device info
		LOGI("--- OpenAL device info ---");
		StringView deviceName = alcGetString(_device, ALC_DEVICE_SPECIFIER);
		StringView renderer = alGetString(AL_RENDERER);
		StringView version = alGetString(AL_VERSION);
		LOGI(deviceName != renderer ? "Device Name: {} ({})" : "Device Name: {}", deviceName, renderer);
		LOGI("OpenAL Version: {}", version);

		ALCint attributesSize = 0;
		alcGetIntegerv(_device, ALC_ATTRIBUTES_SIZE, 1, &attributesSize);
		if (attributesSize > 0) {
			constexpr std::int32_t MaxAttributes = 16;
			ALCint attributes[MaxAttributes * 2];
			const ALCint numAttributes = (attributesSize < MaxAttributes * 2) ? attributesSize : MaxAttributes * 2;

			alcGetIntegerv(_device, ALC_ALL_ATTRIBUTES, numAttributes, attributes);

			ALCint monoSources = 0, stereoSources = 0, efxVersionMajor = 0, efxVersionMinor = 0, efxAuxSends = 0;
			for (std::int32_t i = 0; i + 1 < numAttributes; i += 2) {
				switch (attributes[i]) {
					case ALC_MONO_SOURCES: monoSources = attributes[i + 1]; break;
					case ALC_STEREO_SOURCES: stereoSources = attributes[i + 1]; break;
#if defined(ALC_EFX_MAJOR_VERSION)
					case ALC_EFX_MAJOR_VERSION: efxVersionMajor = attributes[i + 1]; break;
#endif
#if defined(ALC_EFX_MINOR_VERSION)
					case ALC_EFX_MINOR_VERSION: efxVersionMinor = attributes[i + 1]; break;
#endif
#if defined(ALC_MAX_AUXILIARY_SENDS)
					case ALC_MAX_AUXILIARY_SENDS: efxAuxSends = attributes[i + 1]; break;
#endif
				}
			}

#	if defined(ALC_EXT_EFX_NAME)
			bool hasExtEfx = alcIsExtensionPresent(_device, ALC_EXT_EFX_NAME);
#	else
			bool hasExtEfx = alcIsExtensionPresent(_device, "ALC_EXT_EFX");
#	endif
			if (hasExtEfx) {
				LOGI("ALC_EXT_EFX Version: {}.{} ({} auxiliary sends)", efxVersionMajor, efxVersionMinor, efxAuxSends);
			} else {
				LOGI("ALC_EXT_EFX Version: unsupported", hasExtEfx);
			}

#	if defined(AL_DEFAULT_RESAMPLER_SOFT)
			bool defaultResamplerOverriden = false;
			if (alIsExtensionPresent("AL_SOFT_source_resampler")) {
				ALCint defaultResampler = alGetInteger(AL_DEFAULT_RESAMPLER_SOFT);
				String resamplerName = alGetStringiSOFT(AL_RESAMPLER_NAME_SOFT, defaultResampler);
				if (!resamplerName.empty()) {
					LOGI("Resampler: {} ({})", resamplerName, defaultResampler);

					StringUtils::lowercaseInPlace(resamplerName);
					// "Linear" is default in v1.22.2, "Cubic" is default in v1.23.1 and "Cubic Spline" in v1.24.2
					if (resamplerName != "linear"_s && !resamplerName.hasPrefix("cubic"_s)) {
						defaultResamplerOverriden = true;
					}
				}
			}
#	endif

			LOGI("Sources: {} (M) / {} (S)", monoSources, stereoSources);

			for (std::int32_t i = 0; i + 1 < numAttributes; i += 2) {
				switch (attributes[i]) {
					case ALC_FREQUENCY:
						LOGI("Output Frequency: {} Hz", attributes[i + 1]);
						break;
					case ALC_REFRESH:
						LOGI("Refresh Rate: {} Hz", attributes[i + 1]);
						break;
					case ALC_SYNC:
						LOGI("Asynchronous: {}", attributes[i + 1] == ALC_FALSE);
						break;
#	if defined(ALC_SOFT_HRTF)
					case ALC_HRTF_STATUS_SOFT:
						const char* statusStr;
						switch (attributes[i + 1]) {
							case ALC_HRTF_DISABLED_SOFT: statusStr = "disabled"; break;
							case ALC_HRTF_ENABLED_SOFT: statusStr = "enabled"; break;
							case ALC_HRTF_DENIED_SOFT: statusStr = "disabled (denied)"; break;
							case ALC_HRTF_HEADPHONES_DETECTED_SOFT: // Not used by OpenAL Soft anymore
							case ALC_HRTF_REQUIRED_SOFT: statusStr = "enabled (enforced)"; break;
							default: statusStr = "unsupported"; break;
						}
						LOGI("HRTF: {}", statusStr);
						break;
#	endif
				}
			}

#	if defined(AL_DEFAULT_RESAMPLER_SOFT)
			if (defaultResamplerOverriden) {
				LOGW("The default resampler has been overridden on your system, which may result in degraded sound quality");
			}
#	endif
		}
#endif
	}

	ALAudioDevice::~ALAudioDevice()
	{
		LOGD("Disposing OpenAL audio device...");

		// Shut down the decoding thread first, so it doesn't touch any readers afterwards
		shutdownDecodeThread();

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		unregisterAudioEvents();
#endif

#if defined(OPENAL_FILTERS_SUPPORTED)
		for (std::int32_t i = 0; i < MaxSources; i++) {
			if (_filters[i] != 0) {
				alSourcei(_sources[i], AL_DIRECT_FILTER, 0);
				alDeleteFilters(1, &_filters[i]);
				_filters[i] = 0;
			}
		}
#endif

		for (ALuint sourceId : _sources) {
			alSourcei(sourceId, AL_BUFFER, AL_NONE);
		}
		alDeleteSources(MaxSources, _sources);

		alcDestroyContext(_context);

		if (!alcCloseDevice(_device)) {
			LOGW("alcCloseDevice() failed with error 0x{:x}", alGetError());
		}
	}

	bool ALAudioDevice::isValid() const
	{
		return (_device != nullptr);
	}

	const char* ALAudioDevice::name() const
	{
		return _deviceName;
	}

	void ALAudioDevice::setGain(float gain)
	{
		_gain = gain;
		alListenerf(AL_GAIN, _gain);
	}

	void ALAudioDevice::updateListener(const Vector3f& position, const Vector3f& velocity)
	{
		_listenerPos = position;

		alListener3f(AL_POSITION, position.X * LengthToPhysical, position.Y * -LengthToPhysical, position.Z * -LengthToPhysical);
		alListener3f(AL_VELOCITY, velocity.X * VelocityToPhysical, velocity.Y * -VelocityToPhysical, velocity.Z * -VelocityToPhysical);
	}

	std::int32_t ALAudioDevice::nativeFrequency()
	{
		return _nativeFreq;
	}

	std::uint32_t ALAudioDevice::createBuffer(BufferUsage usage)
	{
		alGetError();
		// Through a local of OpenAL's own type: `ALuint` is `unsigned int` while `std::uint32_t` can be
		// `long unsigned int` (it is on MIPS), and taking the address of the caller's variable would then
		// hand the library a pointer to the wrong type even though both are 32 bits wide
		ALuint bufferId = 0;
		alGenBuffers(1, &bufferId);
		const ALenum error = alGetError();
		if DEATH_UNLIKELY(error != AL_NO_ERROR) {
			LOGW("alGenBuffers() failed with error 0x{:x}", error);
			return 0;
		}
		return bufferId;
	}

	void ALAudioDevice::deleteBuffer(std::uint32_t bufferId)
	{
		const ALuint id = bufferId;
		alDeleteBuffers(1, &id);
		AL_LOG_ERRORS();
	}

	bool ALAudioDevice::uploadBuffer(std::uint32_t bufferId, BufferFormat format, const void* data, std::int32_t size, std::int32_t frequency)
	{
		alGetError();
		// On iOS `alBufferDataStatic()` could be used instead
		alBufferData(bufferId, alFormat(format), data, size, frequency);
		const ALenum error = alGetError();
		if DEATH_UNLIKELY(error != AL_NO_ERROR) {
			LOGW("alBufferData() failed with error 0x{:x}", error);
			return false;
		}
		return true;
	}

	void ALAudioDevice::setSourceBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		alSourcei(sourceId, AL_BUFFER, ALint(bufferId));
		AL_LOG_ERRORS();
	}

	void ALAudioDevice::setSourceGain(std::uint32_t sourceId, float gain)
	{
		alSourcef(sourceId, AL_GAIN, gain);
		AL_LOG_ERRORS();
	}

	void ALAudioDevice::setSourcePitch(std::uint32_t sourceId, float pitch)
	{
		alSourcef(sourceId, AL_PITCH, pitch);
		AL_LOG_ERRORS();
	}

	void ALAudioDevice::setSourceLooping(std::uint32_t sourceId, bool looping)
	{
		alSourcei(sourceId, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
		AL_LOG_ERRORS();
	}

	void ALAudioDevice::setSourceRelative(std::uint32_t sourceId, bool relative)
	{
		alSourcei(sourceId, AL_SOURCE_RELATIVE, relative ? AL_TRUE : AL_FALSE);
		AL_LOG_ERRORS();
	}

	void ALAudioDevice::setSourcePosition(std::uint32_t sourceId, const Vector3f& position)
	{
		alSource3f(sourceId, AL_POSITION, position.X, position.Y, position.Z);
		AL_LOG_ERRORS();
	}

#if defined(OPENAL_FILTERS_SUPPORTED)
	ALuint* ALAudioDevice::filterForSource(std::uint32_t sourceId)
	{
		for (std::int32_t i = 0; i < MaxSources; i++) {
			if (_sources[i] == sourceId) {
				return &_filters[i];
			}
		}
		return nullptr;
	}
#endif

	void ALAudioDevice::setSourceLowPass(std::uint32_t sourceId, float value)
	{
#if defined(OPENAL_FILTERS_SUPPORTED)
		ALuint* filterHandle = filterForSource(sourceId);
		if (filterHandle == nullptr) {
			return;
		}

		if (value < 1.0f) {
			if (*filterHandle == 0) {
				alGenFilters(1, filterHandle);
				alFilteri(*filterHandle, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
				alFilterf(*filterHandle, AL_LOWPASS_GAIN, 1.0f);
			}
			if (*filterHandle != 0) {
				alFilterf(*filterHandle, AL_LOWPASS_GAINHF, value);
				alSourcei(sourceId, AL_DIRECT_FILTER, *filterHandle);
			}
		} else if (*filterHandle != 0) {
			alFilterf(*filterHandle, AL_LOWPASS_GAINHF, 1.0f);
			alSourcei(sourceId, AL_DIRECT_FILTER, 0);
		}
		AL_LOG_ERRORS();
#endif
	}

	std::int32_t ALAudioDevice::sourceSampleOffset(std::uint32_t sourceId)
	{
		ALint sampleOffset = 0;
		alGetSourcei(sourceId, AL_SAMPLE_OFFSET, &sampleOffset);
		AL_LOG_ERRORS();
		return sampleOffset;
	}

	void ALAudioDevice::setSourceSampleOffset(std::uint32_t sourceId, std::int32_t offset)
	{
		alSourcei(sourceId, AL_SAMPLE_OFFSET, offset);
		AL_LOG_ERRORS();
	}

	void ALAudioDevice::playSource(std::uint32_t sourceId)
	{
		alSourcePlay(sourceId);
		AL_LOG_ERRORS();
	}

	void ALAudioDevice::pauseSource(std::uint32_t sourceId)
	{
		alSourcePause(sourceId);
		AL_LOG_ERRORS();
	}

	void ALAudioDevice::stopSource(std::uint32_t sourceId)
	{
		alSourceStop(sourceId);
		AL_LOG_ERRORS();
	}

	bool ALAudioDevice::isSourcePlaying(std::uint32_t sourceId)
	{
		ALenum state = AL_STOPPED;
		alGetSourcei(sourceId, AL_SOURCE_STATE, &state);
		AL_LOG_ERRORS();
		return (state == AL_PLAYING);
	}

	void ALAudioDevice::queueBuffer(std::uint32_t sourceId, std::uint32_t bufferId)
	{
		const ALuint id = bufferId;
		alSourceQueueBuffers(sourceId, 1, &id);
		AL_LOG_ERRORS();
	}

	std::int32_t ALAudioDevice::numProcessedBuffers(std::uint32_t sourceId)
	{
		ALint numProcessedBuffers = 0;
		alGetSourcei(sourceId, AL_BUFFERS_PROCESSED, &numProcessedBuffers);
		AL_LOG_ERRORS();
		return numProcessedBuffers;
	}

	void ALAudioDevice::unqueueBuffers(std::uint32_t sourceId, std::int32_t count, std::uint32_t* bufferIds)
	{
		// Through locals of OpenAL's own type, for the same reason as in createBuffer()
		constexpr std::int32_t MaxUnqueuedBuffers = 8;
		ALuint unqueuedIds[MaxUnqueuedBuffers];
		if DEATH_UNLIKELY(count > MaxUnqueuedBuffers) {
			count = MaxUnqueuedBuffers;
		}

		alSourceUnqueueBuffers(sourceId, count, unqueuedIds);
		AL_LOG_ERRORS();

		for (std::int32_t i = 0; i < count; i++) {
			bufferIds[i] = unqueuedIds[i];
		}
	}

#if defined(WITH_LIBRETRO)
	bool ALAudioDevice::renderSamples(std::int16_t* buffer, std::int32_t numFrames)
	{
		if (_device == nullptr) {
			return false;
		}
		_alcRenderSamplesSOFT(_device, buffer, numFrames);
		return true;
	}
#endif

	void ALAudioDevice::suspendDevice()
	{
#if defined(ALC_SOFT_pause_device)
		if (_device != nullptr) {
			alcDevicePauseSOFT(_device);
		}
#endif
	}

	void ALAudioDevice::resumeDevice()
	{
#if defined(ALC_SOFT_pause_device)
		if (_device != nullptr) {
			alcDeviceResumeSOFT(_device);
		}
#endif
	}

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
	void ALAudioDevice::updatePlayers()
	{
		// Audio device cannot be recreated in event callback, so do it here
		if (_shouldRecreate) {
			_shouldRecreate = false;
			recreateAudioDevice();
		}

		AudioDeviceBase::updatePlayers();
	}

	void ALAudioDevice::recreateAudioDevice()
	{
		// Try to use ALC_SOFT_reopen_device extension to reopen the device
		// TODO: If the extension is not present, the device should be fully recreated
		LOGI("Audio device must be recreated due to system changes");
		if (_alcReopenDeviceSOFT != nullptr) {
			if (!_alcReopenDeviceSOFT(_device, nullptr, nullptr)) {
				LOGE("Cannot recreate audio device - alcReopenDeviceSOFT() failed!");
			}

			// Try to get native sample rate of new audio device
			ALCint nativeFreq = 0;
			alcGetIntegerv(_device, ALC_FREQUENCY, 1, &nativeFreq);
			if (nativeFreq >= 44100 && nativeFreq <= 192000) {
				_nativeFreq = nativeFreq;
			}

#	if defined(DEATH_DEBUG) && defined(DEATH_TRACE) && defined(ALC_SOFT_HRTF)
			ALCint status;
			alcGetIntegerv(_device, ALC_HRTF_STATUS_SOFT, 1, &status);
			const char* statusStr;
			switch (status) {
				case ALC_HRTF_DISABLED_SOFT: statusStr = "disabled"; break;
				case ALC_HRTF_ENABLED_SOFT: statusStr = "enabled"; break;
				case ALC_HRTF_DENIED_SOFT: statusStr = "disabled (denied)"; break;
				case ALC_HRTF_HEADPHONES_DETECTED_SOFT: // Not used by OpenAL Soft anymore
				case ALC_HRTF_REQUIRED_SOFT: statusStr = "enabled (enforced)"; break;
				default: statusStr = "unknown"; break;
			}
			LOGD("HRTF: {}", statusStr);
#	endif
		} else {
			LOGE("Cannot recreate audio device - missing extension");
		}
	}

	void ALAudioDevice::registerAudioEvents()
	{
		HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&_pEnumerator));
		if (hr == CO_E_NOTINITIALIZED) {
			LOGW("CoCreateInstance() failed with error CO_E_NOTINITIALIZED");
			hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
			if (FAILED(hr)) {
				hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,  CLSCTX_ALL, IID_PPV_ARGS(&_pEnumerator));
				if (FAILED(hr)) {
					LOGE("CoCreateInstance() failed with error 0x{:.8x}", hr);
				}
			}
		} else if (FAILED(hr)) {
			LOGE("CoCreateInstance() failed with error 0x{:.8x}", hr);
		}

		if (_pEnumerator != nullptr) {
			HRESULT hr = _pEnumerator->RegisterEndpointNotificationCallback(this);
			if (FAILED(hr)) {
				LOGE("RegisterEndpointNotificationCallback() failed with error 0x{:.8x}", hr);
			}
		}
	}

	void ALAudioDevice::unregisterAudioEvents()
	{
		if (_pEnumerator == nullptr) {
			return;
		}

		_pEnumerator->UnregisterEndpointNotificationCallback(this);
		_pEnumerator->Release();
		_pEnumerator = nullptr;
	}

	ULONG ALAudioDevice::AddRef()
	{
		return 1;
	}

	ULONG ALAudioDevice::Release()
	{
		return 1;
	}

	HRESULT ALAudioDevice::QueryInterface(REFIID iid, void** object)
	{
		if (iid == IID_IUnknown || iid == __uuidof(IMMNotificationClient)) {
			*object = static_cast<IMMNotificationClient*>(this);
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	HRESULT ALAudioDevice::OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key)
	{
		return S_OK;
	}

	HRESULT ALAudioDevice::OnDeviceAdded(LPCWSTR pwstrDeviceId)
	{
		return S_OK;
	}

	HRESULT ALAudioDevice::OnDeviceRemoved(LPCWSTR pwstrDeviceId)
	{
		return S_OK;
	}

	HRESULT ALAudioDevice::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState)
	{
		// OnDefaultDeviceChanged() is called afterwards, so no need to handle it here
		//_shouldRecreate = true;
		return S_OK;
	}

	HRESULT ALAudioDevice::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId)
	{
		// Only listen for console device changes
		if (flow != eRender || role != eConsole) {
			return S_OK;
		}

		// If no device is now available, pwstrDefaultDeviceId will be nullptr
		if (pwstrDefaultDeviceId == nullptr) {
			return S_OK;
		}

		std::uint64_t now = Environment::QueryUnbiasedInterruptTimeAsMs();
		String newDeviceId = Utf8::FromUtf16(pwstrDefaultDeviceId);
		if (now - _lastDeviceChangeTime > DeviceChangeLimitMs || newDeviceId != _lastDeviceId) {
			_lastDeviceChangeTime = now;
			_lastDeviceId = std::move(newDeviceId);
			_shouldRecreate = true;
		}

		return S_OK;
	}
#endif
}
