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
		: audioDevice_(&nullAudioDevice_), threadPool_(&nullThreadPool_), rhiCapabilities_(&nullRhiCapabilities_)
	{
	}

	void ServiceLocator::RegisterAudioDevice(std::unique_ptr<IAudioDevice> service)
	{
		registeredAudioDevice_ = std::move(service);
		audioDevice_ = registeredAudioDevice_.get();
	}

	void ServiceLocator::UnregisterAudioDevice()
	{
		registeredAudioDevice_ = nullptr;
		audioDevice_ = &nullAudioDevice_;
	}

	void ServiceLocator::RegisterThreadPool(std::unique_ptr<IThreadPool> service)
	{
		registeredThreadPool_ = std::move(service);
		threadPool_ = registeredThreadPool_.get();
	}

	void ServiceLocator::UnregisterThreadPool()
	{
		registeredThreadPool_ = nullptr;
		threadPool_ = &nullThreadPool_;
	}

	void ServiceLocator::RegisterRhiCapabilities(std::unique_ptr<RHI::IRhiCapabilities> service)
	{
		registeredRhiCapabilities_ = std::move(service);
		rhiCapabilities_ = registeredRhiCapabilities_.get();
	}

	void ServiceLocator::UnregisterRhiCapabilities()
	{
		registeredRhiCapabilities_ = nullptr;
		rhiCapabilities_ = &nullRhiCapabilities_;
	}

	void ServiceLocator::UnregisterAll()
	{
		LOGI("Unregistering all services");

		registeredAudioDevice_ = nullptr;
		audioDevice_ = &nullAudioDevice_;

		registeredThreadPool_ = nullptr;
		threadPool_ = &nullThreadPool_;

		registeredRhiCapabilities_ = nullptr;
		rhiCapabilities_ = &nullRhiCapabilities_;
	}
}
