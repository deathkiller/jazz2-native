#pragma once

#if defined(DEATH_TARGET_DREAMCAST) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace nCine::Backends
{
	/**
		@brief Dreamcast platform helpers on top of KallistiOS

		The console has three separate memories that can each run out on their own: the 16 MB of main
		memory the heap grows into, the 8 MB of video memory the PVR textures are placed in, and the
		2 MB of sound memory the AICA plays samples from. None of them reports itself when it is
		exhausted in a way the log would show, so the state of all three is written out at the moments
		it matters - after a level has loaded and when an allocation has already failed.
	*/
	class DcPlatform
	{
	public:
		DcPlatform() = delete;
		~DcPlatform() = delete;

		/** @brief Writes the state of main, video and sound memory to the log */
		static void LogMemoryStatus(const char* reason);
	};
}

#endif
