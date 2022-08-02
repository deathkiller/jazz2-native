#pragma once

#include <Common.h>

#include <cstdint>

namespace nCine
{
	/// The class interfacing with the system clock
	class Clock
	{
	public:
		/// Returns elapsed time in ticks since base time
		inline uint64_t now() const {
			return counter() - baseCount_;
		}

		/// Returns current value of the counter
		uint64_t counter() const;

		/// Returns the counter frequency in counts per second
		inline uint32_t frequency() const {
			return frequency_;
		}
		/// Retruns the counter value at initialization time
		inline uint64_t baseCount() const {
			return baseCount_;
		}

	private:
#if defined(DEATH_TARGET_WINDOWS)
		bool hasPerfCounter_ = false;
#elif !defined(DEATH_TARGET_APPLE)
		bool hasMonotonicClock_ = false;
#endif

		/// Counter frequency in counts per second
		uint32_t frequency_;
		/// Counter value at initialization time
		uint64_t baseCount_;

		/// Private constructor
		Clock();

		friend Clock& clock();
	};

	// Meyers' Singleton
	extern Clock& clock();

}
