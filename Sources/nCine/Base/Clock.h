#pragma once

#include <Common.h>

namespace nCine
{
	/// System clock
	class Clock
	{
	public:
		inline uint64_t now() const {
			return counter();
		}

		/// Returns current value of the counter
		uint64_t counter() const;

		/// Returns the counter frequency in counts per second
		inline uint32_t frequency() const {
			return _frequency;
		}

	private:
#if defined(DEATH_TARGET_WINDOWS)
		bool _hasPerfCounter;
#elif !defined(DEATH_TARGET_APPLE)
		bool _hasMonotonicClock;
#endif

		/// Counter frequency in counts per second
		uint32_t _frequency;

		/// Private constructor
		Clock();

		friend Clock& clock();
	};

	// Meyers' Singleton
	extern Clock& clock();

}