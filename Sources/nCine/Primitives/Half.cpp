#include "Half.h"

namespace nCine
{
	union FloatBits {
		std::uint32_t u;
		float f;
	};

	// half_to_float_fast4() from https://gist.github.com/rygorous/2144712
	float Half::UnpackHalf(const std::uint16_t value) {
		constexpr const FloatBits Magic{113 << 23};
		// Exponent mask after shift
		constexpr const std::uint32_t ShiftedExp = 0x7c00 << 13;

		const std::uint16_t h{value};
		FloatBits o;

		o.u = (h & 0x7fff) << 13;						// Exponent/mantissa bits
		const std::uint32_t exp = ShiftedExp & o.u;		// Just the exponent
		o.u += (127 - 15) << 23;						// Exponent adjust

		// Handle exponent special cases
		if (exp == ShiftedExp) {						// Inf/NaN?
			o.u += (128 - 16) << 23;					// Extra exp adjust
		} else if (exp == 0) {							// Zero/Denormal
			o.u += 1 << 23;								// Extra exp adjust
			o.f -= Magic.f;								// Renormalize
		}

		o.u |= (h & 0x8000) << 16;						// Sign bit
		return o.f;
	}

	// float_to_half_fast3() from https://gist.github.com/rygorous/2156668
	std::uint16_t Half::PackHalf(const float value)
	{
		constexpr const FloatBits FloatInfinity{255 << 23};
		constexpr const FloatBits HalfInfinity{31 << 23};
		constexpr const FloatBits Magic{15 << 23};
		constexpr const std::uint32_t SignMask = 0x80000000u;
		constexpr const std::uint32_t RoundMask = ~0xfffu;

		FloatBits f;
		f.f = value;
		std::uint16_t h;

		const std::uint32_t sign = f.u & SignMask;
		f.u ^= sign;

		// Note: all the integer compares in this function can be safely compiled
		// into signed compares since all operands are below 0x80000000. Important
		// if you want fast straight SSE2 code (since there's no unsigned PCMPGTD).

		// Inf or NaN (all exponent bits set): NaN->qNaN and Inf->Inf
		if (f.u >= FloatInfinity.u) {
			h = (f.u > FloatInfinity.u) ? 0x7e00 : 0x7c00;
		} else { // (De)normalized number or zero
			f.u &= RoundMask;
			f.f *= Magic.f;
			f.u -= RoundMask;

			// Clamp to signed infinity if overflowed
			if (f.u > HalfInfinity.u) f.u = HalfInfinity.u;

			// Take the bits!
			h = f.u >> 13;
		}

		h |= sign >> 16;
		return h;
	}
}
