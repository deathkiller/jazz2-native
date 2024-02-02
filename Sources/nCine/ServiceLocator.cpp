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

	void ServiceLocator::registerAudioDevice(std::unique_ptr<IAudioDevice> service)
	{
		registeredAudioDevice_ = std::move(service);
		audioDevice_ = registeredAudioDevice_.get();
	}

	void ServiceLocator::unregisterAudioDevice()
	{
		registeredAudioDevice_.reset(nullptr);
		audioDevice_ = &nullAudioDevice_;
	}

	void ServiceLocator::registerThreadPool(std::unique_ptr<IThreadPool> service)
	{
		registeredThreadPool_ = std::move(service);
		threadPool_ = registeredThreadPool_.get();
	}

	void ServiceLocator::unregisterThreadPool()
	{
		registeredThreadPool_.reset(nullptr);
		threadPool_ = &nullThreadPool_;
	}

	void ServiceLocator::registerGfxCapabilities(std::unique_ptr<nCine::IGfxCapabilities> service)
	{
		registeredGfxCapabilities_ = std::move(service);
		gfxCapabilities_ = registeredGfxCapabilities_.get();
	}

	void ServiceLocator::unregisterGfxCapabilities()
	{
		registeredGfxCapabilities_.reset(nullptr);
		gfxCapabilities_ = &nullGfxCapabilities_;
	}

	void ServiceLocator::unregisterAll()
	{
		LOGI("Unregistering all services");

		registeredAudioDevice_.reset(nullptr);
		audioDevice_ = &nullAudioDevice_;

		registeredThreadPool_.reset(nullptr);
		threadPool_ = &nullThreadPool_;

		registeredGfxCapabilities_.reset(nullptr);
		gfxCapabilities_ = &nullGfxCapabilities_;
	}
}
