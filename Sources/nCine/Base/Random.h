#pragma once

#include <cstdint>

namespace nCine
{
	/// PCG32 random number generator
	/*!
	 * Based on the Apache License 2.0 code from pcg-random.org
	 */
	class RandomGenerator
	{
	public:
		/// Creates a new generator with default seeds
		RandomGenerator();
		/// Creates a new generator with the specified seeds
		RandomGenerator(std::uint64_t initState, std::uint64_t initSequence);

		/// Initializes the generator with the specified seeds
		void Initialize(std::uint64_t initState, std::uint64_t initSequence);

		/// Generates a uniformly distributed `32-bit number
		std::uint32_t Next();
		/// Generates a uniformly distributed `32-bit number, r, where min <= r < max
		std::uint32_t Next(std::uint32_t min, std::uint32_t max);
		/// Generates a uniformly distributed float number, r, where 0 <= r < 1
		float NextFloat();
		/// Generates a uniformly distributed float number, r, where min <= r < max
		float NextFloat(float min, float max);

		bool NextBool();

		/// Faster but less uniform version of `integer()`
		std::uint32_t Fast(std::uint32_t min, std::uint32_t max);
		/// Faster but less uniform version of `real()`
		float FastFloat();
		/// Faster but less uniform version of `real()`
		float FastFloat(float min, float max);

	private:
		std::uint64_t _state;
		std::uint64_t _increment;
	};

	// Meyers' Singleton
	extern RandomGenerator& Random();

}