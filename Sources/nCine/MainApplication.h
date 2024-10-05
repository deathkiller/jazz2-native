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

	class MainApplication : public Application
	{
	public:
		/** @brief Entry point method to be called in the `main()`/`wWinMain()` function */ 
		static int Run(std::unique_ptr<IAppEventHandler>(*createAppEventHandler)(), int argc, NativeArgument* argv);

	private:
		bool wasSuspended_;

#if defined(WITH_QT5)
		Qt5Widget* qt5Widget_;
#endif

		/** @brief Must be called at the beginning to initialize the application */
		void Init(std::unique_ptr<IAppEventHandler>(*createAppEventHandler)(), int argc, NativeArgument* argv);
		/** @brief Must be called continuously to keep the application running */
		void ProcessStep();
		/** @brief Processes events inside the game loop */
		void ProcessEvents();
#if defined(DEATH_TARGET_EMSCRIPTEN)
		static void EmscriptenStep();
#endif

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

	Application& theApplication();

}
