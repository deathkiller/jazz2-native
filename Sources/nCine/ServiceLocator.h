#pragma once

#include "Audio/IAudioDevice.h"
#include "Threading/IThreadPool.h"
#include "Graphics/RHI/IRhiCapabilities.h"

namespace nCine
{
	/**
		@brief Central registry that provides engine services to requesting classes
		
		Holds the active audio device, thread pool and RHI capabilities providers. Null implementations
		are used until a real provider is registered, so callers can always obtain a valid reference.
	*/
	class ServiceLocator
	{
	public:
		/** @brief Returns reference to the current audio device instance */
		IAudioDevice& GetAudioDevice() {
			return *audioDevice_;
		}
		/** @brief Registers an audio device provider */
		void RegisterAudioDevice(std::unique_ptr<IAudioDevice> service);
		/** @brief Unregisters the audio device provider and reinstates the null one */
		void UnregisterAudioDevice();

		/** @brief Returns reference to the current thread pool instance */
		IThreadPool& GetThreadPool() {
			return *threadPool_;
		}
		/** @brief Registers a thread pool provider */
		void RegisterThreadPool(std::unique_ptr<IThreadPool> service);
		/** @brief Unregisters the thread pool provider and reinstates the null one */
		void UnregisterThreadPool();

		/** @brief Returns reference to the current RHI capabilities instance */
		const RHI::IRhiCapabilities& GetRhiCapabilities() {
			return *rhiCapabilities_;
		}
		/** @brief Registers an RHI capabilities provider */
		void RegisterRhiCapabilities(std::unique_ptr<RHI::IRhiCapabilities> service);
		/** @brief Unregisters the RHI capabilities provider and reinstates the null one */
		void UnregisterRhiCapabilities();

		/** @brief Unregisters every registered service and reinstates null ones */
		void UnregisterAll();

	private:
		IAudioDevice* audioDevice_;
		std::unique_ptr<IAudioDevice> registeredAudioDevice_;
		NullAudioDevice nullAudioDevice_;

		IThreadPool* threadPool_;
		std::unique_ptr<IThreadPool> registeredThreadPool_;
		NullThreadPool nullThreadPool_;

		RHI::IRhiCapabilities* rhiCapabilities_;
		std::unique_ptr<RHI::IRhiCapabilities> registeredRhiCapabilities_;
		RHI::NullRhiCapabilities nullRhiCapabilities_;

		ServiceLocator();

		ServiceLocator(const ServiceLocator&) = delete;
		ServiceLocator& operator=(const ServiceLocator&) = delete;

		friend ServiceLocator& theServiceLocator();
	};

	/** @brief Returns the singleton service locator instance */
	ServiceLocator& theServiceLocator();

}
