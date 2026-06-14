#pragma once

#include <cstdint>

#include <Containers/ArrayView.h>

using namespace Death;

namespace nCine
{
	/**
		@brief PCG32 pseudo-random number generator
		
		Generates uniformly distributed integers, floats and booleans from a 64-bit state and sequence.
		The @ref Fast() and @ref FastFloat() variants trade statistical uniformity for speed.
	*/
	class RandomGenerator
	{
	public:
		/** @brief Creates a generator seeded with default values */
		RandomGenerator() noexcept;
		/** @brief Creates a generator seeded with the specified state and sequence */
		RandomGenerator(std::uint64_t initState, std::uint64_t initSequence) noexcept;

		/** @brief Reseeds the generator with the specified state and sequence */
		void Init(std::uint64_t initState, std::uint64_t initSequence) noexcept;

		/** @brief Returns a uniformly distributed 32-bit number */
		std::uint32_t Next() noexcept;
		/** @brief Returns a uniformly distributed 32-bit number `r` where `min <= r < max` */
		std::uint32_t Next(std::uint32_t min, std::uint32_t max) noexcept;
		/** @brief Returns a uniformly distributed float `r` where `0 <= r < 1` */
		float NextFloat() noexcept;
		/** @brief Returns a uniformly distributed float `r` where `min <= r < max` */
		float NextFloat(float min, float max) noexcept;
		/** @brief Returns a uniformly distributed boolean */
		bool NextBool() noexcept;

		/** @brief Faster but less uniform version of @ref Next(std::uint32_t, std::uint32_t) */
		std::uint32_t Fast(std::uint32_t min, std::uint32_t max) noexcept;
		/** @brief Faster but less uniform version of @ref NextFloat() */
		float FastFloat() noexcept;
		/** @brief Faster but less uniform version of @ref NextFloat(float, float) */
		float FastFloat(float min, float max) noexcept;

		/** @brief Fills the buffer with a 128-bit unique identifier */
		void Uuid(Containers::StaticArrayView<16, std::uint8_t> result);

		/** @brief Shuffles the elements of the specified array in place */
		template<class T>
		void Shuffle(Containers::ArrayView<T> data) noexcept
		{
			for (std::size_t i = data.size() - 1; i > 0; --i) {
				std::swap(data[i], data[Next(0, (std::uint32_t)i + 1)]);
			}
		}

	private:
		std::uint64_t _state;
		std::uint64_t _increment;
	};

	/** @brief Returns the shared random number generator instance */
	extern RandomGenerator& Random() noexcept;

}