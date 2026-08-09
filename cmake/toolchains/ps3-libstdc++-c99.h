/** @file
	@brief Restores the C99 maths functions `std::` is missing on the PlayStation 3 toolchain

	Force-included into every C++ translation unit by `cmake/toolchains/ps3dev.cmake`, third-party sources
	included. It belongs to the toolchain rather than to the project because the gap it fills is a property
	of the compiler that was installed, not of any code being built.

	The powerpc64-ps3-elf libstdc++ was configured with `_GLIBCXX_USE_C99_MATH_TR1` undefined, so `<cmath>`
	declares none of the C99 maths functions in `std::` even though newlib's `<math.h>` provides them in the
	global namespace. Defining that macro instead is not an option: it also drags in the `long double`
	variants (`log2l`, `logbl`, ...), which this newlib genuinely does not have, and then `<cmath>` fails to
	compile at all. The names actually used are therefore pulled in one by one.

	Only the double forms exist in newlib here, so the float overloads round-trip through double. That is
	what libstdc++ itself does when a target lacks the `f` variants, and on the Cell's PPE - whose FPU is
	double-precision internally and converts on load and store - it costs nothing either way.
*/

#ifndef __JAZZ2_PS3_LIBSTDCXX_C99_H__
#define __JAZZ2_PS3_LIBSTDCXX_C99_H__

#if defined(__cplusplus)

#include <math.h>

namespace std {
	using ::copysign;
	using ::exp2;
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

	inline float copysign(float x, float y) { return static_cast<float>(::copysign(x, y)); }
	inline float exp2(float x) { return static_cast<float>(::exp2(x)); }
	inline float log2(float x) { return static_cast<float>(::log2(x)); }
	inline long lround(float x) { return ::lround(x); }
	inline long long llround(float x) { return ::llround(x); }
	inline float round(float x) { return static_cast<float>(::round(x)); }
	inline float trunc(float x) { return static_cast<float>(::trunc(x)); }
	inline float cbrt(float x) { return static_cast<float>(::cbrt(x)); }
	inline float hypot(float x, float y) { return static_cast<float>(::hypot(x, y)); }
	inline float nearbyint(float x) { return static_cast<float>(::nearbyint(x)); }
	inline float rint(float x) { return static_cast<float>(::rint(x)); }
	inline float fmin(float x, float y) { return static_cast<float>(::fmin(x, y)); }
	inline float fmax(float x, float y) { return static_cast<float>(::fmax(x, y)); }

	// `long double` overloads exist only to keep a call with such an argument unambiguous. Without them the
	// float forms above and newlib's double ones are equally good matches for it, and the call fails to
	// compile rather than picking either. This target's `long double` is the same 64-bit format as `double`,
	// so routing them through it loses nothing.
	inline long double copysign(long double x, long double y) { return ::copysign(double(x), double(y)); }
	inline long double exp2(long double x) { return ::exp2(double(x)); }
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
