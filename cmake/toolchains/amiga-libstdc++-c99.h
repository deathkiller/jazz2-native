/** @file
	@brief Restores the C99 maths functions `std::` is missing on the amiga-gcc toolchain

	Force-included into every C++ translation unit by `cmake/toolchains/amiga.cmake`, third-party
	sources included - the same arrangement as `ps3-libstdc++-c99.h`, and for the same reason: the
	m68k-amigaos libstdc++ is configured with `_GLIBCXX_USE_C99_MATH_TR1` undefined, so `<cmath>`
	declares none of the C99 maths set in `std::` even though libnix's `<math.h>` and libm provide
	nearly all of it in the global namespace.

	Unlike the PS3's newlib, libnix ships the `f` variants (roundf, truncf, fminf, ...), so the float
	overloads here call them directly instead of round-tripping through double - on a 68881/68882
	the single-precision forms are the cheap ones. The three functions libnix genuinely lacks are
	exp2, exp2f and log2f; exp2 is derived from exp (exp2(x) == exp(x * ln 2)) and log2f from the
	double log2 the library does have.
*/

#ifndef __JAZZ2_AMIGA_LIBSTDCXX_C99_H__
#define __JAZZ2_AMIGA_LIBSTDCXX_C99_H__

#if defined(__cplusplus)

#include <math.h>
#include <stddef.h>

// Functions libnix does not declare at all, implemented in Sources/nCine/Backends/Amiga/AmigaLibcCompat.c.
// They are declared here because this header is force-included into every C++ translation unit, which is
// the only place a missing libc declaration can be restored for all of them at once.
extern "C" size_t strnlen(const char* s, size_t maxlen);

namespace std {
	using ::copysign;
	using ::log2;
	using ::lround;
	using ::llround;
	using ::round;
	using ::trunc;
	using ::cbrt;
	using ::hypot;
	using ::nearbyint;
	using ::rint;
	using ::fmin;
	using ::fmax;

	inline float copysign(float x, float y) { return ::copysignf(x, y); }
	inline double exp2(double x) { return ::exp(x * 0.69314718055994530942); }
	inline float exp2(float x) { return ::expf(x * 0.6931471805599453f); }
	inline float log2(float x) { return static_cast<float>(::log2(x)); }
	inline long lround(float x) { return ::lround(x); }
	inline long long llround(float x) { return ::llround(x); }
	inline float round(float x) { return ::roundf(x); }
	inline float trunc(float x) { return ::truncf(x); }
	inline float cbrt(float x) { return ::cbrtf(x); }
	inline float hypot(float x, float y) { return ::hypotf(x, y); }
	inline float nearbyint(float x) { return static_cast<float>(::nearbyint(x)); }
	inline float rint(float x) { return static_cast<float>(::rint(x)); }
	inline float fmin(float x, float y) { return ::fminf(x, y); }
	inline float fmax(float x, float y) { return ::fmaxf(x, y); }

	// `long double` overloads exist only to keep a call with such an argument unambiguous (see the
	// PS3 header); on this target `long double` is wider than double in the 68881's registers, but
	// nothing in the game does long-double maths, so routing through double loses nothing real.
	inline long double copysign(long double x, long double y) { return ::copysign(double(x), double(y)); }
	inline long double exp2(long double x) { return ::exp(double(x) * 0.69314718055994530942); }
	inline long double log2(long double x) { return ::log2(double(x)); }
	inline long lround(long double x) { return ::lround(double(x)); }
	inline long long llround(long double x) { return ::llround(double(x)); }
	inline long double round(long double x) { return ::round(double(x)); }
	inline long double trunc(long double x) { return ::trunc(double(x)); }
	inline long double cbrt(long double x) { return ::cbrt(double(x)); }
	inline long double hypot(long double x, long double y) { return ::hypot(double(x), double(y)); }
	inline long double nearbyint(long double x) { return ::nearbyint(double(x)); }
	inline long double rint(long double x) { return ::rint(double(x)); }
	inline long double fmin(long double x, long double y) { return ::fmin(double(x), double(y)); }
	inline long double fmax(long double x, long double y) { return ::fmax(double(x), double(y)); }
}

#endif

#endif
