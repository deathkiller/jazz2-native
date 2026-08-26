#pragma once

#if defined(WITH_AMIGA)

#include <cstdint>

struct Screen;
struct Window;

namespace nCine::Backends
{
	/**
		@brief Shared AmigaOS plumbing for the Amiga backend classes

		Owns what every backend class needs but none should open twice: the OS library bases and the
		startup performance probe that places the machine on the port's preset ladder. Initialized
		from the platform arm of @ref MainApplication::Run() before any backend object exists, torn
		down after the last one is gone. The EClock (the machine's only fine-grained monotonic clock,
		~709 kHz on PAL) is @b not among them - it belongs to `Death::Environment` (Shared), which opens
		it on the first reading because the shared time queries need it long before any backend exists;
		@ref TimerTicks() is a shortcut to it.

		The Amiga is the one platform in this project whose performance spans two orders of magnitude
		under a single OS and binary - a 68060 at 50 MHz, a Vampire's 68080, a PiStorm's JIT-compiled
		ARM - so nothing here trusts a CPU model number: @ref GetPerformanceClass() is decided by
		MEASURING the two things the game actually spends time on (a blend-shaped memory loop and a
		dependent floating-point chain), which prices the caches, the fast-RAM bus and the JIT into
		the answer automatically. The identification data (AttnFlags, RAM sizes) is logged for
		humans, not branched on.
	*/
	class AmigaPlatform
	{
	public:
		/** @brief How much machine the probe found; picks the preset (resolution, mixing rate, effects) */
		enum class PerformanceClass
		{
			Low,      /**< 68060-class: experimental, smallest screen mode, effects reduced */
			Medium,   /**< Vampire V4-class: low resolution, reduced effects */
			High,     /**< PiStorm Pi3-class: mid resolution, full effects */
			Ultra     /**< PiStorm CM4/Pi4-class: full resolution, everything on */
		};

		AmigaPlatform() = delete;

		/** @brief Opens the OS libraries, runs the performance probe; `false` is fatal for the caller */
		static bool Initialize();
		/**
			@brief Writes what @ref Initialize() found to the log

			Called once the application has attached its trace sink. @ref Initialize() itself runs before
			that - it has to, the performance preset it measures decides the screen mode the graphics
			device opens - so it only *records* its findings and this writes them.
		*/
		static void FlushStartupLog();
		static void Shutdown();

		/** @brief EClock rate in ticks per second (~709379 PAL / ~715909 NTSC), as read by @ref Initialize() */
		static std::uint32_t TimerFrequency();
		/** @brief Current 64-bit EClock value, monotonic for any realistic uptime */
		static std::uint64_t TimerTicks();

		static PerformanceClass GetPerformanceClass();
		/** @brief Overrides the measured class (from a user setting or command-line switch) */
		static void SetPerformanceClass(PerformanceClass performanceClass);

		/** @brief The game screen and its backdrop window, published by @ref AmigaGfxDevice for the input manager */
		static Screen* GameScreen;
		static Window* GameWindow;

		/** @brief `true` once lowlevel.library is open, so joyports can be read */
		static bool HasLowLevel();
		/** @brief `true` once keymap.library is open, so RAWKEY events can become text input */
		static bool HasKeymap();

	private:
		static void RunPerformanceProbe();
	};
}

#endif
