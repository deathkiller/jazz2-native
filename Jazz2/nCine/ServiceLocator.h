#pragma once

#ifdef __ANDROID__
#include <stdarg.h>
#include <android/log.h>
#else
#include <cstdarg>
#endif

#include "IIndexer.h"
//#include "ILogger.h"
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
		/// Returns a reference to the current indexer provider instance
		IIndexer& indexer() {
			return *indexerService_;
		}
		/// Registers an indexer service provider
		void registerIndexer(std::unique_ptr<IIndexer> service);
		/// Unregisters the index service provider and reinstates the null one
		void unregisterIndexer();

		/// Returns a reference to the current logger provider instance
		//ILogger &logger() { return *loggerService_; }
		/// Registers a logger service provider
		//void registerLogger(std::unique_ptr<ILogger> service);
		/// Unregisters the logger service provider and reinstates the null one
		//void unregisterLogger();

		/// Returns a reference to the current audio device instance
		IAudioDevice& audioDevice() {
			return *audioDevice_;
		}
		/// Registers an audio device provider
		void registerAudioDevice(std::unique_ptr<IAudioDevice> service);
		/// Unregisters the audio device provider and reinstates the null one
		void unregisterAudioDevice();

		/// Returns a reference to the current thread pool instance
		IThreadPool& threadPool() {
			return *threadPool_;
		}
		/// Registers a thread pool provider
		void registerThreadPool(std::unique_ptr<IThreadPool> service);
		/// Unregisters the thread pool provider and reinstates the null one
		void unregisterThreadPool();

		/// Returns a reference to the current graphics capabilities instance
		const IGfxCapabilities& gfxCapabilities() {
			return *gfxCapabilities_;
		}
		/// Registers a graphics capabilities provider
		void registerGfxCapabilities(std::unique_ptr<IGfxCapabilities> service);
		/// Unregisters the graphics capabilitiesprovider and reinstates the null one
		void unregisterGfxCapabilities();

		/// Unregisters every registered service and reinstates null ones
		void unregisterAll();

	private:
		IIndexer* indexerService_;
		std::unique_ptr<IIndexer> registeredIndexer_;
		NullIndexer nullIndexer_;

		//ILogger *loggerService_;
		//std::unique_ptr<ILogger> registeredLogger_;
		//NullLogger nullLogger_;

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
		/// Deleted copy constructor
		ServiceLocator(const ServiceLocator&) = delete;
		/// Deleted assignment operator
		ServiceLocator& operator=(const ServiceLocator&) = delete;

		friend ServiceLocator& theServiceLocator();
	};

	/// Meyers' Singleton
	ServiceLocator& theServiceLocator();

}
