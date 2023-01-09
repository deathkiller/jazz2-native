// Copyright © 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016,
//             2017, 2018, 2019, 2020, 2021, 2022
//           Vladimír Vondruš <mosra@centrum.cz> and contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

/** @file
	@brief Intrinsics for x86 LZCNT, BMI1, AVX, AVX F16C, AVX FMA and AVX2 instructions

	Equivalent to @cpp #include <immintrin.h> @ce on most compilers except for GCC
	4.8, where it contains an additional workaround to make the instructions
	available with just the @ref DEATH_ENABLE_AVX, @ref DEATH_ENABLE_AVX_F16C,
	@ref DEATH_ENABLE_AVX_FMA or @ref DEATH_ENABLE_AVX2 function attributes
	instead of having to specify `-mavx` or `-mavx2` for the whole compilation
	unit. This however can't reliably be done for `-mlzcnt`, `-mbmi`, `-mf16c` or
	`-mfma` because then it could not be freely combined with other instruction
	sets, only used alone. You have to enable these instructions globally in order
	to use them on GCC 4.8.

	As AVX-512 is supported only since GCC 4.9, which doesn't need this workaround,
	it's not handled here.
	@see @relativeref{Death,Cpu}, @ref Cpu-usage-target-attributes,
		@ref IntrinsicsSse2.h, @ref IntrinsicsSse3.h, @ref IntrinsicsSsse3.h,
		@ref IntrinsicsSse4.h
*/

// See https://gist.github.com/rygorous/f26f5f60284d9d9246f6 for more info. If I wouldn't need the #define,
// it could be all put into a macro with _Pragma to wrap around the include, but because I do, there has
// to be one wrapper header for each include.

// So users don't need to include the SSE headers explicitly before, in correct order
#include "IntrinsicsSse4.h"

// Include just AVX instructions first. If we would add target("avx2") and
// __AVX2__ here as well, it would cause the AVX instruction to be usable only
// if target("avx2") is specified as well, which is not a good thing for
// DEATH_ENABLE_AVX, where the code should *only* use AVX at most. The AVX2
// instructions are included below by defining target("avx2") and directly
// pulling in <avx2intrin.h>, and only doing that on GCC 4.8, as all other
// compilers have everything already included from the top-level <immintrin.h>.
// Same then goes for F16C, FMA, LZCNT and BMI1, which all also have such weird
// interactions when pulled in together.
// 
// I wonder what impact this has on optimization, but I don't care that much as
// GCC 4.8 is mainly for backwards compatibility testing now, and not serious
// performance use. In any case, one can always compile everything with
// -march=native to get rid of such potential suboptimal optimization.
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__*100 + __GNUC_MINOR__ < 409
#pragma GCC push_options
#pragma GCC target("avx")
#pragma push_macro("__AVX__")
#if !defined(__AVX__)
#	define __AVX__
#endif
#endif
#include <immintrin.h>
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__*100 + __GNUC_MINOR__ < 409
#pragma pop_macro("__AVX__")
#pragma GCC pop_options
#endif

#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__*100 + __GNUC_MINOR__ < 409
#pragma GCC push_options
#pragma GCC target("avx2")
#pragma push_macro("__AVX2__")
#if !defined(__AVX2__)
#	define __AVX2__
#endif
#include <avx2intrin.h>
#pragma pop_macro("__AVX2__")
#pragma GCC pop_options
#endif