#pragma once

#include <cstdint>

#include <Containers/ArrayView.h>

using namespace Death;

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
		RandomGenerator() noexcept;
		/// Creates a new generator with the specified seeds
		RandomGenerator(std::uint64_t initState, std::uint64_t initSequence) noexcept;

		/// Initializes the generator with the specified seeds
		void Initialize(std::uint64_t initState, std::uint64_t initSequence) noexcept;

		/// Generates a uniformly distributed `32-bit number
		std::uint32_t Next() noexcept;
		/// Generates a uniformly distributed `32-bit number, r, where min <= r < max
		std::uint32_t Next(std::uint32_t min, std::uint32_t max) noexcept;
		/// Generates a uniformly distributed float number, r, where 0 <= r < 1
		float NextFloat() noexcept;
		/// Generates a uniformly distributed float number, r, where min <= r < max
		float NextFloat(float min, float max) noexcept;

		bool NextBool() noexcept;

		/// Faster but less uniform version of `integer()`
		std::uint32_t Fast(std::uint32_t min, std::uint32_t max) noexcept;
		/// Faster but less uniform version of `real()`
		float FastFloat() noexcept;
		/// Faster but less uniform version of `real()`
		float FastFloat(float min, float max) noexcept;

		template<class T>
		void Shuffle(Containers::ArrayView<T> data) noexcept
		{
			for (std::size_t i = data.size() - 1; i > 0; --i) {
				std::swap(data[i], data[Next(0, i + 1)]);
			}
		}

	private:
		std::uint64_t _state;
		std::uint64_t _increment;
	};

	// Meyers' Singleton
	extern RandomGenerator& Random() noexcept;

}