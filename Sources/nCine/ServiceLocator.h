#pragma once

#include "Audio/IAudioDevice.h"
#include "Threading/IThreadPool.h"
#include "Graphics/IGfxCapabilities.h"

namespace nCine
{
	/// Provides base services to requesting classes
	/*! It has memory ownership on service classes. */
	class ServiceLocator
	{
	public:
		/// Returns a reference to the current audio device instance
		IAudioDevice& GetAudioDevice() {
			return *audioDevice_;
		}
		/// Registers an audio device provider
		void RegisterAudioDevice(std::unique_ptr<IAudioDevice> service);
		/// Unregisters the audio device provider and reinstates the null one
		void UnregisterAudioDevice();

		/// Returns a reference to the current thread pool instance
		IThreadPool& GetThreadPool() {
			return *threadPool_;
		}
		/// Registers a thread pool provider
		void RegisterThreadPool(std::unique_ptr<IThreadPool> service);
		/// Unregisters the thread pool provider and reinstates the null one
		void UnregisterThreadPool();

		/// Returns a reference to the current graphics capabilities instance
		const IGfxCapabilities& GetGfxCapabilities() {
			return *gfxCapabilities_;
		}
		/// Registers a graphics capabilities provider
		void RegisterGfxCapabilities(std::unique_ptr<IGfxCapabilities> service);
		/// Unregisters the graphics capabilitiesprovider and reinstates the null one
		void UnregisterGfxCapabilities();

		/// Unregisters every registered service and reinstates null ones
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

	/// Meyers' Singleton
	ServiceLocator& theServiceLocator();

}
