#pragma once

#if defined(WITH_PSP) && defined(WITH_CURL)

namespace nCine::Backends
{
	/**
		@brief Socket stack of the PlayStation Portable, which has none of it running at boot

		The two network PRXes have to be loaded and the stack brought up before a single BSD call can work,
		and an access point has to be joined on top of that before anything can leave the console. The two
		halves are deliberately separate, because they cost very different amounts: @ref Initialize() is
		cheap and runs during startup, while joining an access point is seconds of work and is left to
		whichever thread is about to use the network (see @ref EnsureConnected()).
	*/
	class PspNetwork
	{
	public:
		PspNetwork() = delete;
		~PspNetwork() = delete;

		/** @brief Loads the network modules and brings the socket stack up, without joining anything */
		static void Initialize();

		/** @brief Tears the socket stack down again, if it ever came up */
		static void Shutdown();

		/**
			@brief Joins an access point and waits until it has handed out an address

			The association and the DHCP lease behind it are seconds of work, so this is not done during
			startup: every consumer of the network calls it first instead, on its own thread, and the wait
			overlaps with the application coming up rather than stalling in front of it. One association is
			made and shared, so this is cheap to call, safe to call from more than one thread, and returns
			the same answer to all of them.
		*/
		static bool EnsureConnected();
	};
}

#endif
