#include "ALAudioDevice.h"
#include "AudioBufferPlayer.h"
#include "AudioStreamPlayer.h"
#include "../ServiceLocator.h"

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
#	include <Environment.h>
#	include <Utf8.h>
#endif

using namespace Death;

namespace nCine
{
	ALAudioDevice::ALAudioDevice()
		: device_(nullptr), context_(nullptr), gain_(1.0f), sources_ { }, deviceName_(nullptr), nativeFreq_(44100)
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		, alcReopenDeviceSOFT_(nullptr), pEnumerator_(nullptr), lastDeviceChangeTime_(0), shouldRecreate_(false)
#endif
	{
		device_ = alcOpenDevice(nullptr);
		RETURN_ASSERT_MSG(device_ != nullptr, "alcOpenDevice() failed with error 0x%x", alGetError());
		deviceName_ = alcGetString(device_, ALC_DEVICE_SPECIFIER);

		context_ = alcCreateContext(device_, nullptr);
		if (context_ == nullptr) {
			alcCloseDevice(device_);
			RETURN_MSG("alcCreateContext() failed with error 0x%x", alGetError());
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
			RETURN_MSG("alcMakeContextCurrent() failed with error 0x%x", alGetError());
		}

		alGetError();
		alGenSources(MaxSources, sources_);
		const ALenum error = alGetError();
		if (error != AL_NO_ERROR) {
			LOGE("alGenSources() failed with error 0x%x", error);
		} else {
			for (int i = MaxSources - 1; i >= 0; i--) {
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
		// Try to use ALC_SOFT_reopen_device extension to reopen the device
		alcReopenDeviceSOFT_ = (LPALCREOPENDEVICESOFT)alGetProcAddress("alcReopenDeviceSOFT");
		registerAudioEvents();
#endif
	}

	ALAudioDevice::~ALAudioDevice()
	{
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		unregisterAudioEvents();
#endif

		for (ALuint sourceId : sources_) {
			alSourcei(sourceId, AL_BUFFER, AL_NONE);
		}
		alDeleteSources(MaxSources, sources_);

		alcDestroyContext(context_);

		if (!alcCloseDevice(device_)) {
			LOGW("alcCloseDevice() failed with error 0x%x", alGetError());
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

	const IAudioPlayer* ALAudioDevice::player(unsigned int index) const
	{
		if (index < players_.size()) {
			return players_[index];
		}
		return nullptr;
	}

	void ALAudioDevice::stopPlayers()
	{
		for (auto& player : players_) {
			player->stop();
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

		auto it = players_.begin();
		while (it != players_.end()) {
			if ((*it)->type() == objectType) {
				(*it)->stop();
				it = players_.eraseUnordered(it);
				continue;
			}
			++it;
		}
	}

	void ALAudioDevice::pausePlayers(PlayerType playerType)
	{
		const Object::ObjectType objectType = (playerType == PlayerType::Buffer)
			? AudioBufferPlayer::sType()
			: AudioStreamPlayer::sType();

		auto it = players_.begin();
		while (it != players_.end()) {
			if ((*it)->type() == objectType) {
				(*it)->pause();
				it = players_.eraseUnordered(it);
				continue;
			}
			++it;
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

		for (auto& player : players_) {
			player->updateState();
		}
	}

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

	int ALAudioDevice::nativeFrequency()
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
					LOGE("CoCreateInstance() failed with error 0x%08x", hr);
				}
			}
		} else if (FAILED(hr)) {
			LOGE("CoCreateInstance() failed with error 0x%08x", hr);
		}

		if (pEnumerator_ != nullptr) {
			HRESULT hr = pEnumerator_->RegisterEndpointNotificationCallback(this);
			if (FAILED(hr)) {
				LOGE("RegisterEndpointNotificationCallback() failed with error 0x%08x", hr);
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
		shouldRecreate_ = true;
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

		uint64_t now = Environment::QueryUnbiasedInterruptTimeAsMs();
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
