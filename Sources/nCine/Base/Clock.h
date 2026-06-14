#pragma once

#include <Common.h>

namespace nCine
{
	/**
		@brief High-resolution monotonic system clock
		
		Wraps the highest-resolution monotonic timer available on the platform. The single instance
		is accessed through @ref clock().
	*/
	class Clock
	{
	public:
		/** @brief Returns the current counter value */
		inline std::uint64_t now() const {
			return counter();
		}

		/** @brief Returns the current counter value */
		std::uint64_t counter() const;

		/** @brief Returns the counter frequency in counts per second */
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

	/** @brief Returns the shared system clock instance */
	extern Clock& clock();

}