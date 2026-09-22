#pragma once

#include "../CommonConstants.h"

#include "Iterator.h"
#if !defined(NCINE_PREFER_STD_SORT)
#	include "pdqsort/pdqsort.h"
#endif

#include <algorithm>
#include <cmath>
#include <string>

#include <Containers/ArrayView.h>
#include <Containers/StaticArray.h>
#include <Containers/String.h>
#include <Containers/StringView.h>
#include <IO/Stream.h>

using namespace Death;

namespace nCine
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	// Traits
	template<class T>
	struct isIntegral
	{
		static constexpr bool value = false;
	};
	template<>
	struct isIntegral<bool>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<char>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<unsigned char>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<short int>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<unsigned short int>
	{
		static constexpr bool value = true;
	};
	template <>
	struct isIntegral<int>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<unsigned int>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<long>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<unsigned long>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<long long>
	{
		static constexpr bool value = true;
	};
	template<>
	struct isIntegral<unsigned long long>
	{
		static constexpr bool value = true;
	};

	template<class T>
	inline bool IsLess(const T &a, const T &b)
	{
		return a < b;
	}
	
	template<class T>
	inline bool IsNotLess(const T &a, const T &b)
	{
		return !(a < b);
	}

	/// Returns true if the range is sorted into ascending order
	template<class Iterator>
	inline bool isSorted(Iterator first, const Iterator last)
	{
		if (first == last)
			return true;

		Iterator next = first;
		while (++next != last) {
			if (*next < *first)
				return false;
			++first;
		}

		return true;
	}

	/// Returns true if the range is sorted, using a custom comparison
	template<class Iterator, class Compare>
	inline bool isSorted(Iterator first, const Iterator last, Compare comp)
	{
		if (first == last)
			return true;

		Iterator next = first;
		while (++next != last) {
			if (comp(*next, *first))
				return false;
			++first;
		}

		return true;
	}

	/// Returns an iterator to the first element in the range which does not follow an ascending order, or last if sorted
	template<class Iterator>
	inline const Iterator isSortedUntil(Iterator first, const Iterator last)
	{
		if (first == last)
			return first;

		Iterator next = first;
		while (++next != last) {
			if (*next < *first)
				return next;
			++first;
		}

		return last;
	}

	/// Returns an iterator to the first element in the range which does not follow the custom comparison, or last if sorted
	template<class Iterator, class Compare>
	inline const Iterator isSortedUntil(Iterator first, const Iterator last, Compare comp)
	{
		if (first == last)
			return first;

		Iterator next = first;
		while (++next != last) {
			if (comp(*next, *first))
				return next;
			++first;
		}

		return last;
	}

	namespace Implementation
	{
		template<class T>
		struct typeIdentity
		{
			using type = T;
		};

		template<class T>
		auto tryAddRValueReference(int)->typeIdentity<T&&>;
		template<class T>
		auto tryAddRValueReference(...)->typeIdentity<T>;

		template<class T>
		struct addRValueReference : decltype(tryAddRValueReference<T>(0)) {};
		template<class T>
		typename addRValueReference<T>::type declVal();

		/// A container for functions to destruct objects and arrays of objects
		template<bool value>
		struct destructHelpers
		{
			template<class T>
			inline static void destructObject(T* ptr)
			{
				ptr->~T();
			}

			template<class T>
			inline static void destructArray(T* ptr, std::uint32_t numElements)
			{
				for (std::uint32_t i = 0; i < numElements; i++) {
					ptr[numElements - i - 1].~T();
				}
			}
		};

		template<>
		struct destructHelpers<true>
		{
			template<class T>
			inline static void destructObject(T* ptr)
			{
			}

			template<class T>
			inline static void destructArray(T* ptr, unsigned int numElements)
			{
			}
		};
	}

	/// Specialization for trivially destructible types
	template<class T, typename = void>
	struct isDestructible
	{
		static constexpr bool value = false;
	};

	template<class T>
	struct isDestructible<T, decltype(Implementation::declVal<T&>().~T())>
	{
		static constexpr bool value = (true && !__is_union(T));
	};

	// Use `__has_trivial_destructor()` only on GCC
#if defined(__GNUC__) && !defined(__clang__) && !defined(__INTEL_COMPILER)
	template<class T>
	struct hasTrivialDestructor
	{
		static constexpr bool value = __has_trivial_destructor(T);
	};

	template<class T>
	struct isTriviallyDestructible
	{
		static constexpr bool value = isDestructible<T>::value && hasTrivialDestructor<T>::value;
	};
#else
	template<class T>
	struct isTriviallyDestructible
	{
		static constexpr bool value = __is_trivially_destructible(T);
	};
#endif

	template<class T>
	void destructObject(T* ptr)
	{
		Implementation::destructHelpers<isTriviallyDestructible<T>::value>::destructObject(ptr);
	}

	template<class T>
	void destructArray(T* ptr, std::uint32_t numElements)
	{
		Implementation::destructHelpers<isTriviallyDestructible<T>::value>::destructArray(ptr, numElements);
	}
#endif

#if defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_N64) || defined(DEATH_TARGET_DREAMCAST) || \
	defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_PS2) || \
	defined(DEATH_TARGET_PS3) || defined(DEATH_TARGET_AMIGAOS)
	/**
	 * @brief Defined where libm is a software newlib: @ref sinApprox() and friends approximate, @ref floorFast() and friends avoid the call
	 *
	 * Only the PSP and x86-64 have actually been measured; the other consoles are included because they
	 * share the same newlib libm and have no hardware sine (or, on the Dreamcast, have one that libm does
	 * not use). Move a platform out of the list if it ever measures otherwise.
	 */
#	define NCINE_APPROX_TRIG
#endif

	namespace Implementation
	{
#if defined(NCINE_APPROX_TRIG) && !defined(DEATH_TARGET_DREAMCAST)
		/**
		 * @brief Folds an angle into @f$ [-\pi, \pi] @f$ with a single truncating cast, |x| <= 4096
		 *
		 * The nearest whole number of turns is found without a division, an `fmod()` or a `floor()` (MIPS
		 * has no floor instruction, so that would be a real `jal floorf`), in one of two ways:
		 *  - **MIPS (N64, PSP, PS2) and the 3DS**: `trunc(turns + 0.5 + 1024) - 1024`. The bias keeps the
		 *    value positive over the whole guarded range (|turns| < 652), where truncation *is* rounding, so
		 *    there is no branch and no fix-up for negatives, and `trunc.w.s` + `cvt.s.w` stay in the FPU.
		 *    The cast truncates on every one of them, including the PS2's EE, whose FPU can do nothing else
		 *    and rounds all arithmetic toward zero - which is exactly why the other way is NOT used there.
		 *    The bias costs the fraction its low 11 bits, so the rounding decision can be off by 2^-14 of
		 *    a turn and the result overshoot ±Pi by up to 4e-4; the polynomials below stay smooth there.
		 *  - **PowerPC (Wii, GameCube, PS3) and the 68k Amiga**: `(turns + 1.5 * 2^23) - 1.5 * 2^23`, two
		 *    adds that round to the nearest integer in the default round-to-nearest mode, exactly. A float
		 *    to int conversion is what is expensive on these: PowerPC has no FPU-to-FPU conversion, so a
		 *    cast is `fctiwz` + a store + a load + `xoris` + a double load + a subtract, and the 68060 has
		 *    no `fintrz` in hardware at all, it traps to the 68060SP emulation. The trick relies on the
		 *    compiler keeping the two adds, which `-ffast-math` (`-fassociative-math`) would fold away, so
		 *    it is only used when `__FAST_MATH__` is not defined.
		 *
		 * Measured through either fold: 2.3e-4 at 4087 rad and 9e-6 within ±64 rad, both at the resolution
		 * a float has for an angle that big, i.e. the input, not the fold, is the limit.
		 */
		inline float FoldHalfTurn(float x)
		{
#	if (defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_PS3) || defined(DEATH_TARGET_AMIGAOS)) && !defined(__FAST_MATH__)
			constexpr float RoundingBias = 12582912.0f;	// 1.5 * 2^23, the ulp is exactly 1 for |turns| < 2^22
			const float whole = (x * (0.5f / fPi) + RoundingBias) - RoundingBias;
#	else
			// The bias comes back off in float, after the cast: the integer is exact either way, and this keeps
			// the value in the FPU instead of a move to a general register and back for one `addiu`
			const float whole = (float)(std::int32_t)(x * (0.5f / fPi) + 1024.5f) - 1024.0f;
#	endif
			return x - fTwoPi * whole;
		}

		/**
		 * @brief @f$ \sin(x) @f$ for @f$ x \in [-\pi, \pi] @f$ as an odd degree-9 minimax polynomial
		 *
		 * Max error 6.3e-6 over the fold, 1 multiply for x^2, then 4 multiply-adds and a multiply - a
		 * `mul.s` + 4 `madd.s` + `mul.s` on the PS2 and `fmadds` on the Gekko/Broadway. No abs, no select,
		 * no branch: the old parabola-plus-correction needed two of each for 1.1e-3, so this is both
		 * cheaper and 170x more accurate. It is odd, so `SinFolded(-x) == -SinFolded(x)` bit for bit, negative
		 * zero included. Through the fold that holds everywhere except within its 2^-14-turn rounding slop
		 * around odd multiples of Pi, where `x` and `-x` may land on opposite ends of the fold and differ
		 * by the approximation's own error there - a caller that needs an exact `-s` should negate `s` (or, on the
		 * Dreamcast, see the SH4 note in @ref Matrix4x4::RotationZ()).
		 */
		inline float SinFolded(float x)
		{
			const float x2 = x * x;
			return x * (0.999979377f + x2 * (-0.166624382f + x2 * (0.00830898527f + x2 * (-0.000192649939f + x2 * 2.14787201e-06f))));
		}

		/** @brief @f$ \cos(x) @f$ for @f$ x \in [-\pi, \pi] @f$ as an even degree-10 minimax polynomial, max error 1.3e-6 */
		inline float CosFolded(float x)
		{
			const float x2 = x * x;
			return 0.999999225f + x2 * (-0.499994278f + x2 * (0.0416598208f + x2 * (-0.00138589158f + x2 * (2.42043989e-05f + x2 * -2.19788717e-07f))));
		}
#endif

#if defined(DEATH_TARGET_DREAMCAST)
		/**
		 * @brief Both @f$ \sin(x) @f$ and @f$ \cos(x) @f$ from the SH4's `fsca`, |x| <= 4096
		 *
		 * `fsca` reads FPUL as 16.16 fixed-point turns and writes the sine and cosine of the low 16 bits into
		 * a register pair in 3 cycles, so the fold IS the `ftrc` - the whole-turn part simply falls out of
		 * the low half. Accurate to about 2^-21, three orders of magnitude better than any polynomial that
		 * runs in the same time, and it is what KallistiOS' own `fsincos()` does; this is inlined here so
		 * the conversion constant and the range guard match the other targets. `ftrc` saturates rather
		 * than wraps, which the 4096 rad guard (4.3e7 fixed, well inside int32) keeps out of reach.
		 *
		 * `fsca` needs an even-numbered `dr` pair as its destination, hence the pinned registers; fr10/fr11
		 * are caller-saved on the SH ABI and are the pair KallistiOS pins for the same purpose. The `ftrc`
		 * is inside the asm so the fixed-point angle goes FPU -> FPUL directly, without the `sts`/`lds`
		 * round trip through a general register an `int` operand would cost.
		 */
		inline void SinCosFsca(float x, float& s, float& c)
		{
			// Both registers are read-write operands carrying the input in, which is the form KallistiOS'
			// own fsin()/fsincos() use, and the multiply stays inside the asm. The earlier version took the
			// angle in a register of GCC's choosing and declared fr10/fr11 write-only, which left the
			// compiler free to keep an unrelated live value in the pair across the instruction: correct in
			// isolation, but it miscompiled once the surrounding function got busy enough, and the symptom
			// moved whenever anything nearby changed. Nothing here may be reordered or reallocated now.
			register float fs __asm__("fr10") = x;
			register float fc __asm__("fr11") = (65536.0f / fTwoPi);
			__asm__("fmul fr11, fr10\n\t"
					"ftrc fr10, fpul\n\t"
					"fsca fpul, dr10"
					: "+f"(fs), "+f"(fc)
					:
					: "fpul");
			s = fs;
			c = fc;
		}
#endif
	}

	/**
	 * @brief Returns @f$ \sin(x) @f$ as cheaply as the platform can, within 1e-5 of full scale for |x| <= 64
	 *
	 * For values that drive something's appearance or motion - a flicker phase, a pulse, an orbit, a
	 * sprite's rotation, a shot's launch direction - where the library function's last bits buy nothing.
	 * On a platform whose libm is fast this simply calls it, because an approximation is not automatically
	 * cheaper: measured on x86-64 against wrapped phases, a polynomial of this shape was SLOWER than glibc's
	 * `sinf()` (4.9 ns against 3.5 ns, same with clang). It is only worth substituting where libm is
	 * genuinely bad, which on the consoles it is - a `sinf()` on the PSP measures **13.5 us**, about 4,500
	 * cycles, against a few dozen cycles here (see @ref NCINE_APPROX_TRIG for the list).
	 *
	 * What each console gets:
	 *  - **Dreamcast**: the SH4's `fsca` instruction, sine and cosine together at about 2^-21.
	 *  - **Everything else on the list**: a half-turn fold and an odd degree-9 minimax polynomial, 6.3e-6
	 *    over the fold, 9e-6 within ±64 rad. The N64's libdragon ships `fm_sinf()` at a similar accuracy
	 *    and cost; this polynomial is used there too so that every console runs (and is measured on) the
	 *    same code. The PSP's VFPU has `vsin`/`vcos`/`vrot`, but a VFPU context exists only on threads
	 *    created with `PSP_THREAD_ATTR_VFPU`, which the engine's pthread workers and the audio thread are
	 *    not, and the few dozen cycles it would save per call are not worth a function that is unsafe
	 *    off the main thread. The polynomial is already ~250x faster than that libm.
	 *
	 * Precision: at the 6e-6 here, a rotation matrix built from the result is off by less than a 0.001 %
	 * scale, which no sprite in the game can resolve, and a shot's launch angle by 0.0004 degrees. The
	 * only callers that should stay on libm are one-time table generation (already amortized), anything
	 * that uses `sin()` of a huge argument as a hash (it needs the chaotic low bits), and the `sin`/`cos`
	 * registered for level scripts, whose contract is the C library's.
	 *
	 * @param x Angle in radians, @f$ |x| \lesssim 4096 @f$ - past that a float no longer resolves the
	 *   fraction of a turn that is left (an angle of 1e6 has a resolution of 0.06 rad), so larger values are
	 *   handed to libm. Nothing here should pass one, but a phase accumulator that is never wrapped
	 *   eventually would; it is one compare on a path that is already the slow one.
	 */
	inline float sinApprox(float x)
	{
#if defined(DEATH_TARGET_DREAMCAST)
		if (std::fabs(x) > 4096.0f) {
			return std::sin(x);
		}
		float s, c;
		Implementation::SinCosFsca(x, s, c);
		return s;
#elif defined(NCINE_APPROX_TRIG)
		if (std::fabs(x) > 4096.0f) {
			return std::sin(x);
		}
		return Implementation::SinFolded(Implementation::FoldHalfTurn(x));
#else
		return std::sin(x);
#endif
	}

	/** @brief Returns @f$ \cos(x) @f$ as cheaply as the platform can, see @ref sinApprox() */
	inline float cosApprox(float x)
	{
#if defined(DEATH_TARGET_DREAMCAST)
		if (std::fabs(x) > 4096.0f) {
			return std::cos(x);
		}
		float s, c;
		Implementation::SinCosFsca(x, s, c);
		return c;
#elif defined(NCINE_APPROX_TRIG)
		if (std::fabs(x) > 4096.0f) {
			return std::cos(x);
		}
		return Implementation::CosFolded(Implementation::FoldHalfTurn(x));
#else
		return std::cos(x);
#endif
	}

	/**
	 * @brief Returns both @f$ \sin(x) @f$ and @f$ \cos(x) @f$ as cheaply as the platform can, see @ref sinApprox()
	 *
	 * Prefer this wherever both are needed of the same angle: the fold is done once and the two polynomials
	 * share @f$ x^2 @f$, and on the Dreamcast `fsca` produces both in the same instruction anyway.
	 * On a desktop libm this is the `sincosf()` pair the compiler already merges.
	 *
	 * Keep in mind the SH4 codegen note in @ref Matrix4x4::RotationZ(): where a caller needs `-s` as well,
	 * take it from `sinApprox(-x)` on the Dreamcast rather than negating `s`.
	 */
	inline void sincosApprox(float x, float& s, float& c)
	{
#if defined(DEATH_TARGET_DREAMCAST)
		if (std::fabs(x) > 4096.0f) {
			s = std::sin(x);
			c = std::cos(x);
			return;
		}
		Implementation::SinCosFsca(x, s, c);
#elif defined(NCINE_APPROX_TRIG)
		if (std::fabs(x) > 4096.0f) {
			s = std::sin(x);
			c = std::cos(x);
			return;
		}
		x = Implementation::FoldHalfTurn(x);
		s = Implementation::SinFolded(x);
		c = Implementation::CosFolded(x);
#else
		s = std::sin(x);
		c = std::cos(x);
#endif
	}

	/**
	 * @brief Returns @f$ \operatorname{atan2}(y, x) @f$ as cheaply as the platform can, within 1.2e-5 rad
	 *
	 * For a sprite's facing angle or a debris rotation. Newlib's `atan2f()` on the consoles is two software
	 * routines of about 300 instructions with divisions; this is one division, an octant reduction and an
	 * odd degree-9 minimax polynomial on [0, 1] - about 20 instructions, mapping onto multiply-adds. On a
	 * platform with a fast libm (see @ref NCINE_APPROX_TRIG) it calls `std::atan2()` instead.
	 *
	 * Same conventions as the library function: the result is in @f$ [-\pi, \pi] @f$, the sign of `y` picks
	 * the half-plane, and (0, 0) yields 0. Infinities and NaN are not handled.
	 *
	 * **Not used on the Dreamcast**, where this is `std::atan2()`. The approximation is miscompiled there for
	 * negative `y`: measured on hardware over a swept direction, every sample with `y >= 0` was exact and the
	 * ones with `y < 0` were wrong, with the magnitude matching `std::fabs(y)` having been dropped, so the
	 * min/max selection inverted and the ratio came out as `mx / mn` - `atan2Approx(-1.0f, -0.0006f)` returned
	 * -1.9e27 instead of -1.5714. Restructuring it moved the failure rather than removing it (an earlier shape
	 * lost the final sign instead), and the symptom shifted whenever unrelated code in the same function
	 * changed, so the fault is in what the compiler emits rather than in the arithmetic, which is correct on
	 * every other target and on the host. The library call costs more than the polynomial but this is a few
	 * calls a frame, not a hot loop.
	 */
	inline float atan2Approx(float y, float x)
	{
#if defined(NCINE_APPROX_TRIG) && !defined(DEATH_TARGET_DREAMCAST)
		const float ax = std::fabs(x), ay = std::fabs(y);
		const float mx = (ax > ay ? ax : ay), mn = (ax > ay ? ay : ax);
		if (mx == 0.0f) {
			return 0.0f;
		}
		const float a = mn / mx;
		const float a2 = a * a;
		float r = a * (0.999866307f + a2 * (-0.330304652f + a2 * (0.180158854f + a2 * (-0.0851557255f + a2 * 0.0208448265f))));
		if (ay > ax) {
			r = fPiOver2 - r;
		}
		if (x < 0.0f) {
			r = fPi - r;
		}
		// Everything above leaves `r` in [0, Pi], so the half-plane is just the sign of `y`. It is applied with
		// copysign() rather than a conditional negate because that negate was being lost on the Dreamcast: the
		// value came back with the correct magnitude and quadrant but a positive sign for y < 0, which ran the
		// weapon wheel's angle backwards through its whole upper half. Measured on hardware, not guessed -
		// atan2Approx(-0.0084, -1.0) returned +3.13318 where it owes -3.13319. copysign is a sign-bit operation
		// with no branch for the compiler to drop, and it also gives -0.0 for y == -0.0, as atan2() does.
		return std::copysign(r, y);
#else
		return std::atan2(y, x);
#endif
	}

	/**
	 * @brief Returns @f$ \lfloor x \rfloor @f$ without a libm call, for @f$ |x| < 2^{22} @f$
	 *
	 * `std::floor()`, `std::ceil()` and `std::round()` on a float are real calls into newlib on every console
	 * in @ref NCINE_APPROX_TRIG - MIPS and SH-4 have no floor instruction and PowerPC's rounds only to an
	 * integer register through memory - 36 to 68 instructions each, and the collision code and the HUD call
	 * them per actor or per element every frame. These are exact for any value a pixel or tile coordinate can
	 * take; the range limit comes from the float-to-int cast (MIPS, SH-4, ARM) or the 1.5 * 2^23 rounding
	 * trick (PowerPC, 68k, see @ref Implementation::FoldHalfTurn()) they are built on. Elsewhere they are the
	 * library functions, which the compiler turns into one instruction on any SSE4.1 or ARMv8 machine.
	 */
	inline float floorFast(float x)
	{
#if defined(NCINE_APPROX_TRIG)
#	if (defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_PS3) || defined(DEATH_TARGET_AMIGAOS)) && !defined(__FAST_MATH__)
		const float r = (x + 12582912.0f) - 12582912.0f;
#	else
		const float r = (float)(std::int32_t)x;
#	endif
		return (r > x ? r - 1.0f : r);
#else
		return std::floor(x);
#endif
	}

	/** @brief Returns @f$ \lceil x \rceil @f$ without a libm call, for @f$ |x| < 2^{22} @f$, see @ref floorFast() */
	inline float ceilFast(float x)
	{
#if defined(NCINE_APPROX_TRIG)
#	if (defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_PS3) || defined(DEATH_TARGET_AMIGAOS)) && !defined(__FAST_MATH__)
		const float r = (x + 12582912.0f) - 12582912.0f;
#	else
		const float r = (float)(std::int32_t)x;
#	endif
		return (r < x ? r + 1.0f : r);
#else
		return std::ceil(x);
#endif
	}

	/**
	 * @brief Returns @f$ x @f$ rounded to the nearest integer without a libm call, for @f$ |x| < 2^{22} @f$, see @ref floorFast()
	 *
	 * Halfway cases go away from zero like `std::round()`, except within one ulp of the halfway point, where
	 * adding the half can itself round; a position snapped to a pixel cannot tell the difference.
	 */
	inline float roundFast(float x)
	{
#if defined(NCINE_APPROX_TRIG)
		return (x < 0.0f ? -floorFast(0.5f - x) : floorFast(x + 0.5f));
#else
		return std::round(x);
#endif
	}

	/**
	 * @brief Returns @f$ \sqrt{x} @f$ as cheaply as the platform can, and 0 for a non-positive argument
	 *
	 * On the Dreamcast this is the SH4's `fsrra` (reciprocal square root, accurate to the last bits, 1 cycle)
	 * and a multiply, against about 23 cycles for `fsqrt`; `fsrra` of zero is infinity and 0 * inf is NaN,
	 * hence the guard, which also turns a negative argument into 0 where `std::sqrt()` would return NaN.
	 * Every other target has a hardware square root that libm or the compiler already uses, so this is
	 * `std::sqrt()` there, with the same guard for the same answer on every platform.
	 */
	inline float sqrtApprox(float x)
	{
		if (x <= 0.0f) {
			return 0.0f;
		}
#if defined(DEATH_TARGET_DREAMCAST)
		float r = x;
		__asm__("fsrra %0" : "+f"(r));
		return x * r;
#else
		return std::sqrt(x);
#endif
	}

	/** @brief Linearly interpolates between two values by the given ratio */
	inline float lerp(float a, float b, float ratio)
	{
		return a + ratio * (b - a);
	}

	/** @brief Linearly interpolates between two integers by the given ratio, rounding the result */
	inline std::int32_t lerp(std::int32_t a, std::int32_t b, float ratio)
	{
		return (std::int32_t)std::round(a + ratio * (float)(b - a));
	}

	/**
	 * @brief Frame-rate independent interpolation between two values
	 *
	 * Applies @p ratio per nominal frame and scales it by @p timeMult so the result is independent of
	 * the actual frame duration.
	 */
	inline float lerpByTime(float a, float b, float ratio, float timeMult)
	{
		float normalizedRatio = 1.0f - powf(1.0f - ratio, timeMult);
		return a + normalizedRatio * (b - a);
	}

	/**
	 * @brief Copies the beginning of a string into a fixed-size buffer, always null-terminating it
	 *
	 * @param dest		Destination buffer
	 * @param destSize	Size of the destination buffer in bytes
	 * @param source	Source string
	 * @param count		Maximum number of characters to copy, or `-1` to copy the whole source
	 * @return Number of characters written, excluding the null terminator
	 */
	std::int32_t copyStringFirst(char* dest, std::int32_t destSize, const char* source, std::int32_t count = -1);

	/** @brief Copies the beginning of a string into a fixed-size array, deducing its size */
	template<std::size_t size>
	inline std::int32_t copyStringFirst(char(&dest)[size], const char* source, std::int32_t count = -1) {
		return copyStringFirst(dest, size, source, count);
	}

	/** @brief Copies the beginning of a string view into a fixed-size array, deducing its size */
	template<std::size_t size>
	inline std::int32_t copyStringFirst(char(&dest)[size], Containers::StringView source) {
		return copyStringFirst(dest, size, source.data(), (std::int32_t)source.size());
	}

	/**
	 * @brief Writes a `printf`-style formatted string into a buffer
	 *
	 * @return Number of characters written, excluding the null terminator
	 */
	std::int32_t formatString(char* buffer, std::size_t maxLen, const char* format, ...);

	/** @brief Writes a `printf`-style formatted string into a fixed-size array, deducing its size */
	template<std::size_t size, class ...TArg>
	inline std::int32_t formatString(char(&dest)[size], const char* format, const TArg& ...args) {
		return formatString(dest, size, format, args...);
	}

	/** @brief Writes an unsigned 32-bit integer to a buffer as a decimal string */
	void u32tos(std::uint32_t value, char* buffer);
	/** @brief Writes a signed 32-bit integer to a buffer as a decimal string */
	void i32tos(std::int32_t value, char* buffer);
	/** @brief Writes an unsigned 64-bit integer to a buffer as a decimal string */
	void u64tos(std::uint64_t value, char* buffer);
	/** @brief Writes a signed 64-bit integer to a buffer as a decimal string */
	void i64tos(std::int64_t value, char* buffer);
	/** @brief Writes a floating-point value to a buffer as a decimal string */
	void ftos(double value, char* buffer, std::int32_t bufferSize);

	/** @brief Returns `true` if the character is a decimal digit */
	constexpr bool isDigit(char c)
	{
		return (c >= '0' && c <= '9');
	}

	/** @brief Parses up to @p length leading decimal digits into an unsigned 32-bit integer */
	constexpr std::uint32_t stou32(const char* str, std::size_t length)
	{
		std::uint32_t n = 0;
		while (length > 0) {
			if (!isDigit(*str)) {
				break;
			}
			n *= 10;
			n += (*str++ - '0');
			length--;
		}
		return n;
	}

	/** @brief Parses up to @p length leading decimal digits into an unsigned 64-bit integer */
	constexpr std::uint64_t stou64(const char* str, std::size_t length)
	{
		std::uint64_t n = 0;
		while (length > 0) {
			if (!isDigit(*str)) {
				break;
			}
			n *= 10;
			n += (*str++ - '0');
			length--;
		}
		return n;
	}

	/** @brief Sorts the range in place using the given comparator */
	template<class Iter, class Compare>
	inline void sort(Iter begin, Iter end, Compare comp)
	{
#if defined(NCINE_PREFER_STD_SORT)
		std::sort(begin, end, comp);
#else
		pdqsort(begin, end, comp);
#endif
	}

	/** @brief Sorts the range in place into ascending order */
	template<class Iter>
	inline void sort(Iter begin, Iter end)
	{
#if defined(NCINE_PREFER_STD_SORT)
		std::sort(begin, end);
#else
		pdqsort(begin, end);
#endif
	}

	/** @brief Converts a 16-bit half-precision value to a single-precision float */
	float halfToFloat(std::uint16_t value);
	/** @brief Converts a single-precision float to a 16-bit half-precision value */
	std::uint16_t floatToHalf(float value);

	/**
	 * @brief Packs a dotted version string into a single 64-bit number
	 *
	 * Parses a `"major.minor.patch"` string into a 64-bit value with the major part in bits 48-63,
	 * the minor part in bits 32-47 and the patch part in the low 32 bits. A patch part beginning with
	 * `'r'` (a Git revision) is encoded with a special maximum value so it always compares as the
	 * latest.
	 */
	constexpr std::uint64_t parseVersion(Containers::StringView version)
	{
		std::size_t versionLength = version.size();
		if (versionLength == 0) {
			return 0;
		}

		std::size_t dotIndices[3] {};
		std::size_t foundCount = 0;

		for (std::size_t i = 0; i < versionLength; i++) {
			if (version[i] == '.') {
				dotIndices[foundCount++] = i;
				if (foundCount >= Containers::arraySize(dotIndices) - 1) {
					// Save only indices of the first 2 dots and keep the last index for string length
					break;
				}
			}
		}

		dotIndices[foundCount] = versionLength;

		std::uint64_t major = stou32(&version[0], dotIndices[0]);
		std::uint64_t minor = (foundCount >= 1 ? stou32(&version[dotIndices[0] + 1], dotIndices[1] - dotIndices[0] - 1) : 0);
		std::uint64_t patch = (foundCount >= 2
			? (version[dotIndices[1] + 1] != 'r'
				? stou32(&version[dotIndices[1] + 1], dotIndices[2] - dotIndices[1] - 1)
				: 0x0FFFFFFFULL) // GIT Revision - use special value, so it's always the latest (without upper 4 bits)
			: 0);

		return (patch & 0xFFFFFFFFULL) | ((minor & 0xFFFFULL) << 32) | ((major & 0xFFFFULL) << 48);
	}

	/** @brief Encodes the byte range into a URL-safe Base64 string (without padding) */
	template<class Iterator>
	static std::string toBase64Url(const Iterator begin, const Iterator end)
	{
		static const Containers::StaticArray<64, char> chars {
			'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
			'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
			'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
			'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
			'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-', '_'
		};

		std::string result;
		std::size_t c = 0;
		Containers::StaticArray<3, std::uint8_t> charArray;

		for (auto i = begin; i != end; ++i) {
			charArray[c++] = static_cast<std::uint8_t>(*i);
			if (c == 3) {
				result += chars[static_cast<std::uint8_t>((charArray[0] & 0xFC) >> 2)];
				result += chars[static_cast<std::uint8_t>(((charArray[0] & 0x03) << 4) + ((charArray[1] & 0xF0) >> 4))];
				result += chars[static_cast<std::uint8_t>(((charArray[1] & 0x0F) << 2) + ((charArray[2] & 0xC0) >> 6))];
				result += chars[static_cast<std::uint8_t>(charArray[2] & 0x3f)];
				c = 0;
			}
		}

		if (c != 0) {
			result += chars[static_cast<std::uint8_t>((charArray[0] & 0xFC) >> 2)];
			if (c == 1) {
				result += chars[static_cast<std::uint8_t>((charArray[0] & 0x03) << 4)];
			} else { // c == 2
				result += chars[static_cast<std::uint8_t>(((charArray[0] & 0x03) << 4) + ((charArray[1] & 0xF0) >> 4))];
				result += chars[static_cast<std::uint8_t>((charArray[1] & 0x0F) << 2)];
			}
		}

		return result;
	}

#if defined(WITH_ZLIB) || defined(DOXYGEN_GENERATING_OUTPUT)
	/** @brief Returns the CRC-32 checksum of the given byte buffer */
	std::uint32_t crc32(Containers::ArrayView<std::uint8_t> data);
	/** @brief Returns the CRC-32 checksum of the remaining contents of the given stream */
	std::uint32_t crc32(IO::Stream& stream);
#endif
}
