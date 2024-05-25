#pragma once

#include "Application.h"

namespace nCine
{
#if defined(DEATH_TARGET_WINDOWS)
	typedef wchar_t* NativeArgument;
#else
	typedef char* NativeArgument;
#endif

#if defined(WITH_QT5)
	class Qt5Widget;
#endif

	/// Handler class for nCine applications on PC
	class MainApplication : public Application
	{
	public:
		/**  @brief Entry point method to be called in the `main()` function */ 
		static int Run(std::unique_ptr<IAppEventHandler>(*createAppEventHandler)(), int argc, NativeArgument* argv);

		void AttachTraceTarget(Containers::StringView targetPath) override;

	private:
		/// Suspension state from last frame
		bool wasSuspended_;

#if defined(WITH_QT5)
		/// A pointer to the custom Qt5 widget
		Qt5Widget* qt5Widget_;
#endif

		/// Must be called at the beginning to initialize the application
		void Init(std::unique_ptr<IAppEventHandler>(*createAppEventHandler)(), int argc, NativeArgument* argv);
		/// Must be called continuously to keep the application running
		void ProcessStep();
		/// Processes events inside the game loop
		void ProcessEvents();
#if defined(DEATH_TARGET_EMSCRIPTEN)
		static void EmscriptenStep();
#endif

		/// Private constructor
		MainApplication()
			:
			Application(),
#if defined(WITH_QT5)
			qt5Widget_(nullptr),
#endif
			wasSuspended_(false)
		{
		}

		~MainApplication() = default;

		MainApplication(const MainApplication&) = delete;
		MainApplication& operator=(const MainApplication&) = delete;

		friend Application& theApplication();
#if defined(WITH_QT5)
		friend class Qt5Widget;
#endif
	};

	/// Meyers' Singleton
	Application& theApplication();

}
