#pragma once

#if defined(WITH_CTR)

#include <Containers/String.h>

using namespace Death::Containers;

namespace nCine::Backends
{
	/**
		@brief System services and the boot console of the Nintendo 3DS

		Everything the application needs from libctru before any device exists (the graphics service, the
		text console on the bottom screen that shows the startup trace, the CPU speed-up of a New 3DS) and
		the small platform queries the engine asks for later (the console's name, the exit request the system
		delivers through APT), and - when the build has anything to put on the network - the BSD sockets
		service, which has to be handed a buffer of its own before the first socket is opened. Brought up by
		`MainApplication::Run()` before `Init()`, exactly like `AmigaPlatform` on the classic Amiga.
	*/
	class CtrPlatform
	{
	public:
		/** @brief Brings up the system services and the boot console; returns `false` if the console cannot run the game */
		static bool Initialize();
		/** @brief Drains the APT event queue; returns `false` once the system asked the application to leave */
		static bool Update();
		/** @brief Shuts the system services down again (after every device is gone) */
		static void Shutdown();

		/**
			@brief Writes a trace line to the console on the bottom screen

			Every line while the renderer has not taken the display over yet, so a startup failure is
			readable without any host tool; afterwards only the lines flagged @p important (warnings and
			errors), because the console rasterizes its text with the CPU the game is using - a level load's
			thousands of I/O trace lines would cost more than the load itself.
		*/
		static void WriteBootConsole(const char* text, std::int32_t length, bool important);
		/** @brief Switches the boot console to important lines only (called once the renderer owns the display) */
		static void SetBootConsoleQuiet(bool quiet);

		/** @brief Returns the user name the console was given in its settings, or its unique ID, or an empty string */
		static String GetDeviceName();
		/** @brief Returns `true` on a New 3DS / New 2DS (the 804 MHz models with the C-Stick and ZL/ZR) */
		static bool IsNew3DS();
		/** @brief Returns `true` when the BSD sockets service is up (see @ref Initialize()) */
		static bool IsNetworkAvailable();

	private:
		static bool _initialized;
		static bool _bootConsoleQuiet;
		static bool _isNew3DS;
		static void* _socketBuffer;
	};
}

#endif
