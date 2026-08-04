#include "ServiceLocator.h"
#include "../Main.h"

namespace nCine
{
	ServiceLocator& theServiceLocator()
	{
		static ServiceLocator instance;
		return instance;
	}

	ServiceLocator::ServiceLocator()
		: _audioDevice(&_nullAudioDevice), _threadPool(&_nullThreadPool), _rhiCapabilities(&_nullRhiCapabilities)
	{
	}

	void ServiceLocator::RegisterAudioDevice(std::unique_ptr<IAudioDevice> service)
	{
		_registeredAudioDevice = std::move(service);
		_audioDevice = _registeredAudioDevice.get();
	}

	void ServiceLocator::UnregisterAudioDevice()
	{
		_registeredAudioDevice = nullptr;
		_audioDevice = &_nullAudioDevice;
	}

	void ServiceLocator::RegisterThreadPool(std::unique_ptr<IThreadPool> service)
	{
		_registeredThreadPool = std::move(service);
		_threadPool = _registeredThreadPool.get();
	}

	void ServiceLocator::UnregisterThreadPool()
	{
		_registeredThreadPool = nullptr;
		_threadPool = &_nullThreadPool;
	}

	void ServiceLocator::RegisterRhiCapabilities(std::unique_ptr<RHI::IRhiCapabilities> service)
	{
		_registeredRhiCapabilities = std::move(service);
		_rhiCapabilities = _registeredRhiCapabilities.get();
	}

	void ServiceLocator::UnregisterRhiCapabilities()
	{
		_registeredRhiCapabilities = nullptr;
		_rhiCapabilities = &_nullRhiCapabilities;
	}

	void ServiceLocator::UnregisterAll()
	{
		LOGI("Unregistering all services");

		_registeredAudioDevice = nullptr;
		_audioDevice = &_nullAudioDevice;

		_registeredThreadPool = nullptr;
		_threadPool = &_nullThreadPool;

		_registeredRhiCapabilities = nullptr;
		_rhiCapabilities = &_nullRhiCapabilities;
	}
}
