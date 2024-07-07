#include "Random.h"
#include "../../Common.h"

#include <cmath> // for ldexp()

namespace nCine
{
	namespace
	{
		const std::uint64_t DefaultInitState = 0x853c49e6748fea9bULL;
		const std::uint64_t DefaultInitSequence = 0xda3e39cb94b95bdbULL;

		std::uint32_t random(std::uint64_t& state, std::uint64_t& increment)
		{
			const std::uint64_t oldState = state;
			state = oldState * 6364136223846793005ULL + increment;
			const std::uint32_t xorShifted = static_cast<std::uint32_t>(((oldState >> 18u) ^ oldState) >> 27u);
			const std::uint32_t rotation = static_cast<std::uint32_t>(oldState >> 59u);
			return (xorShifted >> rotation) | (xorShifted << ((std::uint32_t)(-(std::int32_t)rotation) & 31));
		}

		std::uint32_t boundRandom(std::uint64_t& state, std::uint64_t& increment, std::uint32_t bound)
		{
			const std::uint32_t threshold = (std::uint32_t)(-(std::int32_t)bound) % bound;
			while (true) {
				const std::uint32_t r = random(state, increment);
				if (r >= threshold) {
					return r % bound;
				}
			}
		}
	}

	RandomGenerator& Random()
	{
		static RandomGenerator instance;
		return instance;
	}

	RandomGenerator::RandomGenerator()
		: RandomGenerator(DefaultInitState, DefaultInitSequence)
	{
	}

	RandomGenerator::RandomGenerator(std::uint64_t initState, std::uint64_t initSequence)
		: _state(0ULL), _increment(0ULL)
	{
		Initialize(initState, initSequence);
	}

	void RandomGenerator::Initialize(std::uint64_t initState, std::uint64_t initSequence)
	{
		_state = 0U;
		_increment = (initSequence << 1u) | 1u;
		random(_state, _increment);
		_state += initState;
		random(_state, _increment);
	}

	std::uint32_t RandomGenerator::Next()
	{
		return random(_state, _increment);
	}

	std::uint32_t RandomGenerator::Next(std::uint32_t min, std::uint32_t max)
	{
		if (min == max) {
			return min;
		} else {
			return min + boundRandom(_state, _increment, max - min);
		}
	}

	float RandomGenerator::NextFloat()
	{
		return static_cast<float>(ldexp(random(_state, _increment), -32));
	}

	float RandomGenerator::NextFloat(float min, float max)
	{
		return min + static_cast<float>(ldexp(random(_state, _increment), -32)) * (max - min);
	}

	bool RandomGenerator::NextBool()
	{
		return (boundRandom(_state, _increment, 2) != 0);
	}

	std::uint32_t RandomGenerator::Fast(std::uint32_t min, std::uint32_t max)
	{
		return (min == max ? min : min + random(_state, _increment) % (max - min));
	}

	float RandomGenerator::FastFloat()
	{
		return static_cast<float>(random(_state, _increment) / static_cast<float>(UINT32_MAX));
	}

	float RandomGenerator::FastFloat(float min, float max)
	{
		return min + (static_cast<float>(random(_state, _increment)) / static_cast<float>(UINT32_MAX)) * (max - min);
	}
}
