#pragma once

#include <Common.h>

namespace nCine
{
	/// System clock
	class Clock
	{
	public:
		inline std::uint64_t now() const {
			return counter();
		}

		/// Returns current value of the counter
		std::uint64_t counter() const;

		/// Returns the counter frequency in counts per second
		inline std::uint32_t frequency() const {
			return _frequency;
		}

	private:
#if defined(DEATH_TARGET_WINDOWS)
		bool _hasPerfCounter;
#elif !defined(DEATH_TARGET_APPLE)
		bool _hasMonotonicClock;
#endif
		std::uint32_t _frequency;

		Clock();

		friend Clock& clock();
	};

	/// Returns system clock instance
	extern Clock& clock();

}