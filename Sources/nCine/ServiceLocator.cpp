#include "ServiceLocator.h"
#include "../Common.h"

namespace nCine
{
	ServiceLocator& theServiceLocator()
	{
		static ServiceLocator instance;
		return instance;
	}

	ServiceLocator::ServiceLocator()
		: audioDevice_(&nullAudioDevice_), threadPool_(&nullThreadPool_), gfxCapabilities_(&nullGfxCapabilities_)
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

	void ServiceLocator::RegisterGfxCapabilities(std::unique_ptr<nCine::IGfxCapabilities> service)
	{
		registeredGfxCapabilities_ = std::move(service);
		gfxCapabilities_ = registeredGfxCapabilities_.get();
	}

	void ServiceLocator::UnregisterGfxCapabilities()
	{
		registeredGfxCapabilities_ = nullptr;
		gfxCapabilities_ = &nullGfxCapabilities_;
	}

	void ServiceLocator::UnregisterAll()
	{
		LOGI("Unregistering all services");

		registeredAudioDevice_ = nullptr;
		audioDevice_ = &nullAudioDevice_;

		registeredThreadPool_ = nullptr;
		threadPool_ = &nullThreadPool_;

		registeredGfxCapabilities_ = nullptr;
		gfxCapabilities_ = &nullGfxCapabilities_;
	}
}
