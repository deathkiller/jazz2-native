#pragma once

#include "Audio/IAudioDevice.h"
#include "Threading/IThreadPool.h"
#include "Graphics/IGfxCapabilities.h"

namespace nCine
{
	/** @brief Provides base services to requesting classes */
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

		/** @brief Returns reference to the current graphics capabilities instance */
		const IGfxCapabilities& GetGfxCapabilities() {
			return *gfxCapabilities_;
		}
		/** @brief Registers a graphics capabilities provider */
		void RegisterGfxCapabilities(std::unique_ptr<IGfxCapabilities> service);
		/** @brief Unregisters the graphics capabilitiesprovider and reinstates the null one */
		void UnregisterGfxCapabilities();

		/** @brief Unregisters every registered service and reinstates null ones */
		void UnregisterAll();

	private:
		IAudioDevice* audioDevice_;
		std::unique_ptr<IAudioDevice> registeredAudioDevice_;
		NullAudioDevice nullAudioDevice_;

		IThreadPool* threadPool_;
		std::unique_ptr<IThreadPool> registeredThreadPool_;
		NullThreadPool nullThreadPool_;

		IGfxCapabilities* gfxCapabilities_;
		std::unique_ptr<IGfxCapabilities> registeredGfxCapabilities_;
		NullGfxCapabilities nullGfxCapabilities_;

		ServiceLocator();

		ServiceLocator(const ServiceLocator&) = delete;
		ServiceLocator& operator=(const ServiceLocator&) = delete;

		friend ServiceLocator& theServiceLocator();
	};

	ServiceLocator& theServiceLocator();

}
