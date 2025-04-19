#pragma once

#include <cstdint>

#include <Containers/ArrayView.h>

using namespace Death;

namespace nCine
{
	/// Random number generator
	class RandomGenerator
	{
	public:
		/// Creates a new generator with default seeds
		RandomGenerator() noexcept;
		/// Creates a new generator with the specified seeds
		RandomGenerator(std::uint64_t initState, std::uint64_t initSequence) noexcept;

		/// Initializes the generator with the specified seeds
		void Initialize(std::uint64_t initState, std::uint64_t initSequence) noexcept;

		/// Generates a uniformly distributed 32-bit number
		std::uint32_t Next() noexcept;
		/// Generates a uniformly distributed 32-bit number, r, where min <= r < max
		std::uint32_t Next(std::uint32_t min, std::uint32_t max) noexcept;
		/// Generates a uniformly distributed float number, r, where 0 <= r < 1
		float NextFloat() noexcept;
		/// Generates a uniformly distributed float number, r, where min <= r < max
		float NextFloat(float min, float max) noexcept;
		/// Generates a uniformly distributed boolean
		bool NextBool() noexcept;

		/// Faster but less uniform version of @ref Next()
		std::uint32_t Fast(std::uint32_t min, std::uint32_t max) noexcept;
		/// Faster but less uniform version of @ref NextFloat()
		float FastFloat() noexcept;
		/// Faster but less uniform version of @ref NextFloat()
		float FastFloat(float min, float max) noexcept;

		/// Gnerates a 128-bit unique identifier
		void Uuid(Containers::StaticArrayView<16, std::uint8_t> result);

		/// Shuffles the specified array
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

	/// Returns random number generator instance
	extern RandomGenerator& Random() noexcept;

}