#include "../../Main.h"

#if defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_N64) || defined(DEATH_TARGET_PS2) || defined(DEATH_TARGET_PSP)

#include <cstdint>
#include <cstring>

#if defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_PS2) || defined(DEATH_TARGET_PSP)

// Replacements for the double-precision math library functions the module player spends its time in, on the
// consoles whose FPU makes a `double` expensive. The PSP's Allegrex and the PS2's Emotion Engine compute in single
// precision only, so every double operation there is a libgcc soft-float routine; the Dreamcast's SH-4 does doubles
// in hardware, but KallistiOS builds for its single-precision-default ABI (-m4-single), so each one is bracketed by
// a switch of FPSCR's precision mode that stalls the pipeline. newlib's `pow()` and `log()` are fdlibm's
// double-precision algorithms, a few hundred such operations each - 68.6 us and 29.4 us a call measured on a PSP -
// and libxmp calls them for every playing channel on every tick of a module (`libxmp_note_to_period()` twice per
// note, `libxmp_period_to_bend()` two of those and a `log()` on top). On the PSP that was the larger half of the
// decoding thread, a tenth of the whole CPU in a level with busy music; on the PS2, which decodes music on the main
// thread, it was a millisecond or two of every frame.
//
// The platform arms of cmake/ncine_extra_sources.cmake link the executable with `--wrap=pow,--wrap=log` (and
// `--wrap=__divdf3` where noted below), so these definitions receive every call in the executable and answer the
// ordinary ones in single precision: the logarithm through the exponent and a short series in (m-1)/(m+1), the
// power through a rounded exponent and a series on what is left. Both are within a few units in the last place of
// a float (the EE rounds every operation toward zero, which costs one more at most), so a note's period comes out
// within 5e-7 of the double result and a pitch bend within 0.02 of libxmp's units, a ten-thousandth of a cent.
// Rendering every module of the game both ways gave 95% to 99.97% of the samples bit-identical, and no other
// difference than the occasional sample a voice reaches one output frame earlier or later.
//
// Anything outside the plain positive range - a negative or zero base, a result that would leave the float range,
// NaN or infinity - goes to newlib, and so does a whole exponent: the module player's Vorbis decoder (stb_vorbis,
// for modules with compressed samples) sizes its codebooks with `floor(pow(r, dim))` and needs an integral power
// to come out exact, which the series would miss by an ulp now and then. The one whole power libxmp itself asks
// for is a power of two - `pow(2, n / 12)` of every C note - and that is built exactly from its exponent bits.
namespace
{
	// log2 of a positive, normal, finite float
	inline float FastLog2(float x)
	{
		std::uint32_t bits;
		std::memcpy(&bits, &x, sizeof(bits));
		std::int32_t exponent = std::int32_t((bits >> 23) & 0xFF) - 127;
		bits = (bits & 0x007FFFFFu) | 0x3F800000u;
		float m;
		std::memcpy(&m, &bits, sizeof(m));
		if (m > 1.41421356f) {
			m *= 0.5f;
			exponent++;
		}
		// 2/ln(2) * atanh(t) as its odd series, |t| <= 0.1716
		const float t = (m - 1.0f) / (m + 1.0f);
		const float t2 = t * t;
		return float(exponent) + t * (2.88539008f + t2 * (0.961796694f + t2 * (0.577078016f + t2 * (0.412198583f + t2 * 0.320598898f))));
	}

	// 2^y for |y| < 126
	inline float FastExp2(float y)
	{
		const std::int32_t whole = (y >= 0.0f ? std::int32_t(y + 0.5f) : -std::int32_t(0.5f - y));
		// e^(f*ln(2)) as its Taylor series, |f| <= 0.5
		const float f = y - float(whole);
		const float p = 1.0f + f * (0.693147181f + f * (0.240226507f + f * (0.0555041087f + f * (0.00961812911f +
			f * (0.00133335581f + f * (0.000154035304f + f * 0.0000152527338f))))));
		const std::uint32_t scaleBits = std::uint32_t(whole + 127) << 23;
		float scale;
		std::memcpy(&scale, &scaleBits, sizeof(scale));
		return p * scale;
	}

	// Whether a value converted from a double is a normal float with room to spare. Tested after the conversion,
	// because on the soft-float consoles a double comparison is a library call of its own. A double too large for a
	// float converts to infinity, one too small to zero (or a denormal, which the EE reads as zero), and NaN fails
	// every comparison - the EE, which has neither infinity nor NaN, reads both bit patterns as values of magnitude
	// 2^128 and above, so they still fail the upper bound.
	inline bool IsInFastRange(float x)
	{
		return (x >= 1.0e-36f && x <= 1.0e36f);
	}

	constexpr std::uint64_t DoubleTwoBits = 0x4000000000000000ull;
}

extern "C" double __real_pow(double x, double y);
extern "C" double __real_log(double x);
extern "C" double __wrap_pow(double x, double y);
extern "C" double __wrap_log(double x);

extern "C" double __wrap_pow(double x, double y)
{
	const float xf = float(x);
	const float yf = float(y);
	if (IsInFastRange(xf) && yf >= -1.0e6f && yf <= 1.0e6f) {
		const std::int32_t whole = std::int32_t(yf);
		if (float(whole) != yf) {
			const float exponent = yf * FastLog2(xf);
			if (exponent > -120.0f && exponent < 120.0f) {
				return double(FastExp2(exponent));
			}
		} else if (whole >= -1022 && whole <= 1023) {
			// A whole power of two, exactly - compared by bits, a double comparison being a library call here
			std::uint64_t xBits;
			std::memcpy(&xBits, &x, sizeof(xBits));
			const double wholeAsDouble = double(whole);
			if (xBits == DoubleTwoBits && std::memcmp(&wholeAsDouble, &y, sizeof(y)) == 0) {
				const std::uint64_t resultBits = std::uint64_t(whole + 1023) << 52;
				double result;
				std::memcpy(&result, &resultBits, sizeof(result));
				return result;
			}
		}
	}
	return __real_pow(x, y);
}

extern "C" double __wrap_log(double x)
{
	const float xf = float(x);
	if (IsInFastRange(xf)) {
		return double(FastLog2(xf) * 0.693147181f);
	}
	return __real_log(x);
}

#endif

#if defined(DEATH_TARGET_PS2) || defined(DEATH_TARGET_PSP)

// Double division gets a replacement on the two consoles where it is soft-float, for a different reason: libgcc's
// generic `__divdf3` produces the quotient a bit at a time and costs about 5.4 us on a PSP, while libxmp divides
// several times per playing voice on every tick (the voice's sample step, then how far it is from the sample's end)
// - some 6000 divisions a second in a level with busy music. This one is EXACT, not an approximation: the quotient
// comes out ten bits per step, each step's digit estimated in single precision from the top bits of the remainder
// and then corrected against the full divisor in 64-bit integers, so the remainder at the end is exact and the
// rounding (to nearest, ties to even) is decided from it like an FPU would. Checked bit for bit against hardware
// division on 250 million operand pairs; how far off a digit estimate is only decides how often the correction
// loops run, so the EE's round-toward-zero arithmetic cannot change a result either. Only normal operands with a
// normal result take this way; zeros, subnormals, infinities, NaNs and results that underflow or overflow go to
// libgcc.
namespace
{
	bool FastDivide(double a, double b, double& result)
	{
		std::uint64_t ua, ub;
		std::memcpy(&ua, &a, sizeof(ua));
		std::memcpy(&ub, &b, sizeof(ub));
		const std::uint32_t ea = std::uint32_t(ua >> 52) & 0x7FF;
		const std::uint32_t eb = std::uint32_t(ub >> 52) & 0x7FF;
		if (ea - 1 >= 0x7FE || eb - 1 >= 0x7FE) {
			return false;
		}
		constexpr std::uint64_t ImplicitBit = std::uint64_t(1) << 52;
		std::uint64_t n = (ua & (ImplicitBit - 1)) | ImplicitBit;
		const std::uint64_t d = (ub & (ImplicitBit - 1)) | ImplicitBit;
		std::int32_t exponent = std::int32_t(ea) - std::int32_t(eb) + 1023;
		if (n < d) {
			n <<= 1;
			exponent--;
		}

		// n/d is in [1, 2), so the leading quotient bit is 1 and the 52 below it come out ten at a time. The
		// remainder stays below d * 2^10 < 2^63, so its top word fits a signed integer, and the estimate is off by
		// at most one or two either way - the two loops settle it.
		std::uint64_t r = n - d;
		std::uint64_t q = 1;
		const float invDivisorTop = 1.0f / float(std::int32_t(d >> 32));
		for (std::int32_t bits = 52; bits > 0; ) {
			const std::int32_t step = (bits < 10 ? bits : 10);
			bits -= step;
			r <<= step;
			// Through a signed integer: neither CPU has a float-to-unsigned conversion
			std::uint32_t digit = std::uint32_t(std::int32_t(float(std::int32_t(r >> 32)) * invDivisorTop));
			std::uint64_t product = std::uint64_t(digit) * d;
			while (product > r) {
				digit--;
				product -= d;
			}
			r -= product;
			while (r >= d) {
				digit++;
				r -= d;
			}
			q = (q << step) | digit;
		}

		const std::uint64_t twiceRemainder = r << 1;
		if (twiceRemainder > d || (twiceRemainder == d && (q & 1) != 0)) {
			q++;
			if (q == (ImplicitBit << 1)) {
				q >>= 1;
				exponent++;
			}
		}
		if (exponent <= 0 || exponent >= 0x7FF) {
			return false;
		}
		const std::uint64_t bits = ((ua ^ ub) & (std::uint64_t(1) << 63)) | (std::uint64_t(exponent) << 52) | (q & (ImplicitBit - 1));
		std::memcpy(&result, &bits, sizeof(result));
		return true;
	}
}

extern "C" double __real___divdf3(double a, double b);
extern "C" double __wrap___divdf3(double a, double b);

extern "C" double __wrap___divdf3(double a, double b)
{
	double result;
	if (FastDivide(a, b, result)) {
		return result;
	}
	return __real___divdf3(a, b);
}

#endif


// `floorf()` and `ceilf()` have no instruction on any of these CPUs, so they are newlib's out-of-line routines that
// take the float apart bit by bit - about 1.9 us each on the N64, a few tenths of a microsecond on the PSP and the
// PS2. The generated fixed-function effects call them several times per band of the textured background (`floor()`
// of the shader language), which made `floorf()` one of the larger items of such a level's frame on the N64. Both
// are linked through `--wrap` where that measured as a win (see the platform arms of cmake/ncine_extra_sources.cmake;
// the SH-4's own `floorf()` is already cheaper than this one), so these definitions receive every call there and
// answer them with a conversion to an integer and back: exact for every magnitude below 2^23, above which every
// float is already a whole number. Bit for bit what newlib returns for every float that is not a NaN (checked
// against the host's libm over all 2^32 patterns), down to the sign of a zero result. A NaN comes back as it went
// in, where newlib's `x + x` would have raised the unimplemented-operation exception the VR4300 takes on any NaN
// operand.
namespace
{
	union FloatBits
	{
		float Value;
		std::uint32_t Bits;
	};

	// The sign of a zero result has to survive -ffast-math (whose -fno-signed-zeros lets the compiler treat a
	// literal -0.0f as +0.0f), so it comes from its bit pattern
	inline float NegativeZero()
	{
		FloatBits result;
		result.Bits = 0x80000000u;
		return result.Value;
	}
}

extern "C" float __wrap_floorf(float x);
extern "C" float __wrap_ceilf(float x);

extern "C" float __wrap_floorf(float x)
{
	FloatBits value;
	value.Value = x;
	const std::uint32_t exponent = (value.Bits >> 23) & 0xFFu;
	if (exponent >= 127 + 23) {
		return x;	// A whole number already, an infinity or a NaN
	}
	if (exponent < 127) {
		// |x| < 1: a zero keeps its sign, anything else is 0 or -1
		if ((value.Bits & 0x7FFFFFFFu) == 0) {
			return x;
		}
		return ((value.Bits >> 31) != 0 ? -1.0f : 0.0f);
	}
	const float truncated = float(std::int32_t(x));
	return (truncated > x ? truncated - 1.0f : truncated);
}

extern "C" float __wrap_ceilf(float x)
{
	FloatBits value;
	value.Value = x;
	const std::uint32_t exponent = (value.Bits >> 23) & 0xFFu;
	if (exponent >= 127 + 23) {
		return x;	// A whole number already, an infinity or a NaN
	}
	if (exponent < 127) {
		// |x| < 1: a zero keeps its sign, anything else is 1 or -0
		if ((value.Bits & 0x7FFFFFFFu) == 0) {
			return x;
		}
		return ((value.Bits >> 31) != 0 ? NegativeZero() : 1.0f);
	}
	const float truncated = float(std::int32_t(x));
	return (truncated < x ? truncated + 1.0f : truncated);
}

#endif
