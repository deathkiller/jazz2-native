#pragma once

#if defined(DEATH_TARGET_PSP)

#include <cstdint>

namespace nCine::Backends
{
	/**
		@brief Sleep and wake of the console, as the firmware reports them

		A press of the power switch - or of the Vita's power button while Adrenaline runs the game - suspends
		the whole process and resumes it later, with every thread frozen wherever it was. The firmware tells
		the application through a power callback, and what does not survive the sleep has to be handled by
		whoever owns it - the audio channel, for one, has to be reserved again (see @ref PspAudioDevice).

		The callback runs on the firmware's callback thread, with a stack of a few kilobytes and the game's
		own threads anywhere, so it only updates what the functions below read; each subsystem polls them
		from its own thread and reacts there.
	*/
	class PspPower
	{
	public:
		PspPower() = delete;
		~PspPower() = delete;

		/**
			@brief Registers the power callback

			Has to be called from the thread that waits for firmware callbacks (in `sceKernelSleepThreadCB()`),
			because a callback is only ever delivered to the thread that created it.
		*/
		static void RegisterCallback();

		/** @brief Returns `true` from the moment the console starts going to sleep until it has fully woken up */
		static bool IsSuspended();

		/** @brief Returns how many times the console has woken up from sleep, so a subsystem can notice each wake once */
		static std::uint32_t GetResumeCount();
	};
}

#endif
