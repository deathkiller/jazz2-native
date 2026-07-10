#include "ALAudioDevice.h"
#include "AudioBufferPlayer.h"
#include "AudioStreamPlayer.h"
#include "../ServiceLocator.h"

#include <Containers/StringUtils.h>

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
#	include <Environment.h>
#	include <Utf8.h>
#endif

using namespace Death;
using namespace Death::Containers::Literals;

namespace nCine
{
	ALAudioDevice::ALAudioDevice()
		: device_(nullptr), context_(nullptr), gain_(1.0f), sources_ {}, deviceName_(nullptr), nativeFreq_(44100)
#if defined(WITH_THREADS)
		, decodeThreadCreated_(false), decodeThreadShouldQuit_(false)
#endif
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		, alcReopenDeviceSOFT_(nullptr), pEnumerator_(nullptr), lastDeviceChangeTime_(0), shouldRecreate_(false)
#endif
	{
		Init();
	}

	void ALAudioDevice::Init()
	{
		LOGD("Initializing OpenAL audio device...");

		device_ = alcOpenDevice(nullptr);
		DEATH_ASSERT(device_ != nullptr, ("alcOpenDevice() failed with error 0x{:x}", alGetError()), );
		deviceName_ = alcGetString(device_, ALC_DEVICE_SPECIFIER);

		context_ = alcCreateContext(device_, nullptr);
		if (context_ == nullptr) {
			alcCloseDevice(device_);
			LOGE("alcCreateContext() failed with error 0x{:x}", alGetError());
			return;
		}

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		// Try to get native sample rate of default audio device (default is 44100)
		ALCint nativeFreq = 0;
		alcGetIntegerv(device_, ALC_FREQUENCY, 1, &nativeFreq);
		if (nativeFreq >= 44100 && nativeFreq <= 192000) {
			nativeFreq_ = nativeFreq;
		}
#endif

		if (!alcMakeContextCurrent(context_)) {
			alcDestroyContext(context_);
			alcCloseDevice(device_);
			LOGE("alcMakeContextCurrent() failed with error 0x{:x}", alGetError());
			return;
		}

		alGetError();
		alGenSources(MaxSources, sources_);
		const ALenum error = alGetError();
		if (error != AL_NO_ERROR) {
			LOGE("alGenSources() failed with error 0x{:x}", error);
		} else {
			for (std::int32_t i = MaxSources - 1; i >= 0; i--) {
				sourcePool_.push_back(sources_[i]);
			}
		}

		alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
		alDopplerFactor(1.0f);
		alSpeedOfSound(360.0f);
		alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
		alListenerf(AL_GAIN, gain_);

#if defined(AL_STOP_SOURCES_ON_DISCONNECT_SOFT) && !defined(DEATH_TARGET_EMSCRIPTEN)
		// Don't stop sources when device is disconnected if supported
		alDisable(AL_STOP_SOURCES_ON_DISCONNECT_SOFT);
#endif

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		// Try to use ALC_SOFT_reopen_device extension to reopen the device, it's a context
		// extension, so it has to be queried with alcGetProcAddress() and the device
		if (alcIsExtensionPresent(device_, "ALC_SOFT_reopen_device")) {
			alcReopenDeviceSOFT_ = (LPALCREOPENDEVICESOFT)alcGetProcAddress(device_, "alcReopenDeviceSOFT");
		}
		registerAudioEvents();
#endif

#if defined(DEATH_TRACE)
		// Log OpenAL device info
		LOGI("--- OpenAL device info ---");
		StringView deviceName = alcGetString(device_, ALC_DEVICE_SPECIFIER);
		StringView renderer = alGetString(AL_RENDERER);
		StringView version = alGetString(AL_VERSION);
		LOGI(deviceName != renderer ? "Device Name: {} ({})" : "Device Name: {}", deviceName, renderer);
		LOGI("OpenAL Version: {}", version);

		ALCint attributesSize = 0;
		alcGetIntegerv(device_, ALC_ATTRIBUTES_SIZE, 1, &attributesSize);
		if (attributesSize > 0) {
			constexpr std::int32_t MaxAttributes = 16;
			ALCint attributes[MaxAttributes * 2];
			const ALCint numAttributes = (attributesSize < MaxAttributes * 2) ? attributesSize : MaxAttributes * 2;

			alcGetIntegerv(device_, ALC_ALL_ATTRIBUTES, numAttributes, attributes);

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
			bool hasExtEfx = alcIsExtensionPresent(device_, ALC_EXT_EFX_NAME);
#	else
			bool hasExtEfx = alcIsExtensionPresent(device_, "ALC_EXT_EFX");
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

#if defined(WITH_THREADS)
		// Shut down the decoding thread first, so it doesn't touch any readers afterwards
		decodeMutex_.Lock();
		decodeThreadShouldQuit_ = true;
		// Requests still in the queue will never be executed, reset them so their owners don't wait forever
		for (auto& request : decodeQueue_) {
			request->state.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
		}
		decodeQueue_.clear();
		decodeMutex_.Unlock();
		decodeQueueCond_.Broadcast();
		if (decodeThreadCreated_) {
			decodeThread_.Join();
		}
#endif

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		unregisterAudioEvents();
#endif

		for (ALuint sourceId : sources_) {
			alSourcei(sourceId, AL_BUFFER, AL_NONE);
		}
		alDeleteSources(MaxSources, sources_);

		alcDestroyContext(context_);

		if (!alcCloseDevice(device_)) {
			LOGW("alcCloseDevice() failed with error 0x{:x}", alGetError());
		}
	}

	bool ALAudioDevice::isValid() const
	{
		return (device_ != nullptr);
	}

	const char* ALAudioDevice::name() const
	{
		return deviceName_;
	}

	void ALAudioDevice::setGain(ALfloat gain)
	{
		gain_ = gain;
		alListenerf(AL_GAIN, gain_);
	}

	const IAudioPlayer* ALAudioDevice::player(std::uint32_t index) const
	{
		if (index < players_.size()) {
			return players_[index];
		}
		return nullptr;
	}

	IAudioPlayer* ALAudioDevice::player(std::uint32_t index)
	{
		if (index < players_.size()) {
			return players_[index];
		}
		return nullptr;
	}

	void ALAudioDevice::stopPlayers()
	{
		// Iterating backwards because stop() unregisters the player, erasing it from the array
		for (std::size_t i = players_.size(); i > 0; i--) {
			players_[i - 1]->stop();
		}
		players_.clear();
	}

	void ALAudioDevice::pausePlayers()
	{
		for (auto& player : players_) {
			player->pause();
		}
		players_.clear();
	}

	void ALAudioDevice::stopPlayers(PlayerType playerType)
	{
		const Object::ObjectType objectType = (playerType == PlayerType::Buffer)
			? AudioBufferPlayer::sType()
			: AudioStreamPlayer::sType();

		// Iterating backwards because stop() unregisters the player, erasing it from the array
		for (std::size_t i = players_.size(); i > 0; i--) {
			IAudioPlayer* player = players_[i - 1];
			if (player->type() == objectType) {
				player->stop();
			}
		}
	}

	void ALAudioDevice::pausePlayers(PlayerType playerType)
	{
		const Object::ObjectType objectType = (playerType == PlayerType::Buffer)
			? AudioBufferPlayer::sType()
			: AudioStreamPlayer::sType();

		// Iterating backwards, so removing the current element doesn't affect the remaining ones
		for (std::size_t i = players_.size(); i > 0; i--) {
			IAudioPlayer* player = players_[i - 1];
			if (player->type() == objectType) {
				player->pause();
				players_.eraseUnordered(&players_[i - 1]);
			}
		}
	}

	void ALAudioDevice::freezePlayers()
	{
		for (auto& player : players_) {
			player->pause();
		}
		// The players array is not cleared at this point, it is needed as-is by the unfreeze method
	}

	void ALAudioDevice::unfreezePlayers()
	{
		for (auto& player : players_) {
			player->play();
		}
	}

	unsigned int ALAudioDevice::registerPlayer(IAudioPlayer* player)
	{
		if (sourcePool_.empty()) {
			return UnavailableSource;
		}

		ALuint sourceId = sourcePool_.pop_back_val();

		if (players_.size() < MaxSources) {
			players_.push_back(player);
		}

		return sourceId;
	}

	void ALAudioDevice::unregisterPlayer(IAudioPlayer* player)
	{
		if (player->sourceId_ == UnavailableSource) {
			return;
		}

		sourcePool_.push_back(player->sourceId_);
		player->sourceId_ = UnavailableSource;

		auto it = players_.begin();
		while (it != players_.end()) {
			if (*it == player) {
				players_.erase(it);
				break;
			}
			++it;
		}
	}

	void ALAudioDevice::updatePlayers()
	{
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		// Audio device cannot be recreated in event callback, so do it here
		if (shouldRecreate_) {
			shouldRecreate_ = false;
			recreateAudioDevice();
		}
#endif

		// Iterating backwards because a finished player unregisters itself, erasing it from the array
		for (std::size_t i = players_.size(); i > 0; i--) {
			players_[i - 1]->updateState();
		}
	}

	bool ALAudioDevice::submitStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request)
	{
#if defined(WITH_THREADS)
		decodeMutex_.Lock();
		if (!decodeThreadCreated_) {
			// The decoding thread is created lazily on the first streamed sound
			decodeThreadCreated_ = true;
			decodeThread_ = Thread(ALAudioDevice::decodeThreadFunc, this);
		}
		decodeQueue_.push_back(request);
		decodeMutex_.Unlock();
		decodeQueueCond_.Signal();
		return true;
#else
		return false;
#endif
	}

	void ALAudioDevice::drainStreamDecode(const std::shared_ptr<StreamDecodeRequest>& request)
	{
#if defined(WITH_THREADS)
		// A null request would compare equal to the idle active request and wait forever
		if (request == nullptr) {
			return;
		}

		decodeMutex_.Lock();
		// Remove the request from the queue if it hasn't been picked up yet
		for (std::size_t i = 0; i < decodeQueue_.size(); i++) {
			if (decodeQueue_[i] == request) {
				decodeQueue_.erase(&decodeQueue_[i]);
				request->state.store(StreamDecodeRequest::State::Idle, std::memory_order_relaxed);
				decodeMutex_.Unlock();
				return;
			}
		}
		// Wait for the decoding thread if the request is currently being executed
		while (activeDecodeRequest_ == request) {
			decodeDoneCond_.Wait(decodeMutex_);
		}
		decodeMutex_.Unlock();
#endif
	}

#if defined(WITH_THREADS)
	void ALAudioDevice::decodeThreadFunc(void* arg)
	{
		Thread::SetCurrentName("Audio decoding");

		ALAudioDevice* device = static_cast<ALAudioDevice*>(arg);
		device->decodeMutex_.Lock();
		while (true) {
			while (device->decodeQueue_.empty() && !device->decodeThreadShouldQuit_) {
				device->decodeQueueCond_.Wait(device->decodeMutex_);
			}
			if (device->decodeThreadShouldQuit_) {
				break;
			}

			device->activeDecodeRequest_ = std::move(device->decodeQueue_.front());
			device->decodeQueue_.erase(device->decodeQueue_.begin());
			device->decodeMutex_.Unlock();

			// Decoding is executed without holding the lock, so new requests can still be submitted
			device->activeDecodeRequest_->Execute();

			device->decodeMutex_.Lock();
			device->activeDecodeRequest_ = nullptr;
			device->decodeDoneCond_.Broadcast();
		}
		device->decodeMutex_.Unlock();
	}
#endif

	const Vector3f& ALAudioDevice::getListenerPosition() const
	{
		return _listenerPos;
	}

	void ALAudioDevice::updateListener(const Vector3f& position, const Vector3f& velocity)
	{
		_listenerPos = position;

		alListener3f(AL_POSITION, position.X * LengthToPhysical, position.Y * -LengthToPhysical, position.Z * -LengthToPhysical);
		alListener3f(AL_VELOCITY, velocity.X * VelocityToPhysical, velocity.Y * -VelocityToPhysical, velocity.Z * -VelocityToPhysical);
	}

	std::int32_t ALAudioDevice::nativeFrequency()
	{
		return nativeFreq_;
	}

	void ALAudioDevice::suspendDevice()
	{
#if defined(ALC_SOFT_pause_device)
		if (device_ != nullptr) {
			alcDevicePauseSOFT(device_);
		}
#endif
	}

	void ALAudioDevice::resumeDevice()
	{
#if defined(ALC_SOFT_pause_device)
		if (device_ != nullptr) {
			alcDeviceResumeSOFT(device_);
		}
#endif
	}

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
	void ALAudioDevice::recreateAudioDevice()
	{
		// Try to use ALC_SOFT_reopen_device extension to reopen the device
		// TODO: If the extension is not present, the device should be fully recreated
		LOGI("Audio device must be recreated due to system changes");
		if (alcReopenDeviceSOFT_ != nullptr) {
			if (!alcReopenDeviceSOFT_(device_, nullptr, nullptr)) {
				LOGE("Cannot recreate audio device - alcReopenDeviceSOFT() failed!");
			}

			// Try to get native sample rate of new audio device
			ALCint nativeFreq = 0;
			alcGetIntegerv(device_, ALC_FREQUENCY, 1, &nativeFreq);
			if (nativeFreq >= 44100 && nativeFreq <= 192000) {
				nativeFreq_ = nativeFreq;
			}

#	if defined(DEATH_DEBUG) && defined(DEATH_TRACE) && defined(ALC_SOFT_HRTF)
			ALCint status;
			alcGetIntegerv(device_, ALC_HRTF_STATUS_SOFT, 1, &status);
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
		HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pEnumerator_));
		if (hr == CO_E_NOTINITIALIZED) {
			LOGW("CoCreateInstance() failed with error CO_E_NOTINITIALIZED");
			hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
			if (FAILED(hr)) {
				hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,  CLSCTX_ALL, IID_PPV_ARGS(&pEnumerator_));
				if (FAILED(hr)) {
					LOGE("CoCreateInstance() failed with error 0x{:.8x}", hr);
				}
			}
		} else if (FAILED(hr)) {
			LOGE("CoCreateInstance() failed with error 0x{:.8x}", hr);
		}

		if (pEnumerator_ != nullptr) {
			HRESULT hr = pEnumerator_->RegisterEndpointNotificationCallback(this);
			if (FAILED(hr)) {
				LOGE("RegisterEndpointNotificationCallback() failed with error 0x{:.8x}", hr);
			}
		}
	}

	void ALAudioDevice::unregisterAudioEvents()
	{
		if (pEnumerator_ == nullptr) {
			return;
		}

		pEnumerator_->UnregisterEndpointNotificationCallback(this);
		pEnumerator_->Release();
		pEnumerator_ = nullptr;
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
		//shouldRecreate_ = true;
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
		if (now - lastDeviceChangeTime_ > DeviceChangeLimitMs || newDeviceId != lastDeviceId_) {
			lastDeviceChangeTime_ = now;
			lastDeviceId_ = std::move(newDeviceId);
			shouldRecreate_ = true;
		}

		return S_OK;
	}
#endif
}
