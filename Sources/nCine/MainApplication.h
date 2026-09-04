#pragma once

#include "Application.h"

#if defined(DEATH_TARGET_PSP)
#	include <psputility.h>
#elif defined(DEATH_TARGET_VITA)
#	include <psp2/ime_dialog.h>
#endif

namespace nCine
{
#if defined(DEATH_TARGET_PSP)
	/** @brief Stops the Allegrex FPU trapping on IEEE exceptions; must run on every thread */
	void PspDisableFpuTraps(bool trace);
#endif

	/** @brief Native argument type, `wchar_t*` on Windows, otherwise `char*` */
#if defined(DEATH_TARGET_WINDOWS)
	typedef wchar_t* NativeArgument;
#else
	typedef char* NativeArgument;
#endif

#if defined(WITH_QT5)
	namespace Backends { class Qt5Widget; }
#endif

	/**
		@brief Main entry point and event loop driver for standard (desktop) applications
		
		Concrete @ref Application subclass that initializes the engine, runs the game loop and processes
		platform events on desktop backends (GLFW/SDL/Qt5).
	*/
	class MainApplication : public Application
	{
	public:
		/** @brief Entry point to be called from the `main()`/`wWinMain()` function; returns the process exit code */
		static int Run(CreateAppEventHandlerDelegate createAppEventHandler, int argc, NativeArgument* argv);

		void Quit() override;

		bool EnablePlayStationExtendedSupport(bool enable) override;
		String GetUserName() override;
		bool OpenUrl(StringView url) override;
		
#if defined(DEATH_TARGET_VITA) || defined(DEATH_TARGET_PSP) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
			@brief Advances the on-screen keyboard, turning a finished IME dialog into text input events

			The PS Vita has no keyboard overlay that feeds keystrokes - it has the IME, a modal dialog that
			collects a whole string - so the result has to be polled and handed to the input event handler as
			the text input the caller expected. Called once per frame from @ref Run(). The PSP's `sceUtility`
			on-screen keyboard is the same kind of dialog and is driven the same way (its drawing is the one
			part that lives elsewhere: the GU backend refreshes it at present time, see `GuDevice::PresentFrame()`).
		*/
		void UpdateScreenKeyboard();
#endif

		bool CanShowScreenKeyboard() override;
		bool IsScreenKeyboardVisible() override;
		bool ToggleScreenKeyboard() override;
		bool ShowScreenKeyboard(Containers::StringView initialText = {},
			Containers::Function<void(Containers::StringView)>&& onCompleted = {}) override;
		bool HideScreenKeyboard() override;

	private:
#if defined(DEATH_TARGET_PSP)
		/** @brief Longest string the on-screen keyboard collects (a player name or a server address, not prose) */
		static constexpr std::size_t ImeMaxTextLength = 256;

		// The keyboard reads and writes these for as long as the dialog is up (UCS-2, the firmware's text
		// encoding), so they belong to the application rather than to the call that opened it
		SceUtilityOskParams _oskParams;
		SceUtilityOskData _oskData;
		unsigned short _oskInputBuffer[ImeMaxTextLength + 1];
		unsigned short _oskOutputBuffer[ImeMaxTextLength + 1];
		unsigned short _oskTitle[32];
		/** Whether a keyboard opened by @ref ShowScreenKeyboard() has not been collected yet */
		bool _oskActive = false;
		/** Invoked with the finished string when the dialog is confirmed, see @ref ShowScreenKeyboard() */
		Containers::Function<void(Containers::StringView)> _imeOnCompleted;
#elif defined(DEATH_TARGET_VITA)
		/** @brief Longest string the on-screen keyboard collects (a player name or a server address, not prose) */
		static constexpr std::size_t ImeMaxTextLength = 256;

		// The IME writes into these for as long as the dialog is up, so they belong to the application
		// rather than to the call that opened it
		SceWChar16 _imeInputBuffer[ImeMaxTextLength + 1];
		SceWChar16 _imeTitle[32];
		/** Invoked with the finished string when the dialog is confirmed, see @ref ShowScreenKeyboard() */
		Containers::Function<void(Containers::StringView)> _imeOnCompleted;
#endif

		bool _wasSuspended;

#if defined(WITH_QT5)
		Backends::Qt5Widget* _qt5Widget;
#endif

		/** @brief Must be called at the beginning to initialize the application */
		void Init(CreateAppEventHandlerDelegate createAppEventHandler, int argc, NativeArgument* argv);
		/** @brief Must be called continuously to keep the application running */
		void ProcessStep();
#if defined(WITH_GLFW) || (defined(WITH_SDL2) || defined(WITH_SDL3))
		/** @brief Processes events inside the game loop */
		void ProcessEvents();
#endif
#if defined(DEATH_TARGET_EMSCRIPTEN)
		static void EmscriptenStep();
#endif

		MainApplication()
			:
			Application(),
#if defined(WITH_QT5)
			_qt5Widget(nullptr),
#endif
			_wasSuspended(false)
		{
		}

		~MainApplication() = default;

		MainApplication(const MainApplication&) = delete;
		MainApplication& operator=(const MainApplication&) = delete;

		friend Application& theApplication();
#if defined(WITH_QT5)
		friend class Backends::Qt5Widget;
#endif
	};

	/** @brief Returns the singleton application instance */
	Application& theApplication();

}
