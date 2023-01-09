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
	@brief Intrinsics for x86 SSE4.1, SSE4.2 and POPCNT instructions

	Equivalent to @cpp #include <smmintrin.h> @ce and
	@cpp #include <nmmintrin.h> @ce on most compilers except for:

	-   GCC 4.8, where it contains an additional workaround to make the
		instructions available with just the @ref DEATH_ENABLE_SSE41 and
		@ref DEATH_ENABLE_SSE42 function attributes instead of having to specify
		`-msse4.1` or `-msse4.2` for the whole compilation unit. This however can't
		reliably be done for `-mpopcnt` because then it could not be freely
		combined with other instruction sets, only used alone. You have to enable
		these instructions globally in order to use them on GCC 4.8.
	-   Clang < 7, where `__POPCNT__` has to be explicitly defined in order to
		access the POPCNT instruction

	Because GCC puts both the SSE4.1 and the SSE4.2 instructions into the same
	header and just guards each with a different macro, they have to be included
	together, unlike with other SSE variants.
	@see @relativeref{Death,Cpu}, @ref Cpu-usage-target-attributes,
		@ref IntrinsicsSse2.h, @ref IntrinsicsSse3.h, @ref IntrinsicsSsse3.h,
		@ref IntrinsicsAvx.h
*/

// See https://gist.github.com/rygorous/f26f5f60284d9d9246f6 for more info. If I wouldn't need the #define,
// it could be all put into a macro with _Pragma to wrap around the include, but because I do, there has to be one
// wrapper header for each include.

// So users don't need to include the SSE2, SSE3 and SSSE3 headers explicitly before, in correct order
#include "IntrinsicsSsse3.h"

// Unfortunately, as opposed to AVX, the SSE4.1 and 4.2 intrinsics live in the exact same header, which for GCC 4.8 means
// we have to subsequently have target("sse4.2") again in order to call even SSE4.1. This is the reason why
// DEATH_ENABLE_SSE41 is not defined there.
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__*100 + __GNUC_MINOR__ < 409
#pragma GCC push_options
#pragma GCC target("sse4.1")
#pragma GCC target("sse4.2")
#pragma push_macro("__SSE4_1__")
#pragma push_macro("__SSE4_2__")
#if !defined(__SSE4_1__)
#	define __SSE4_1__
#endif
#if !defined(__SSE4_2__)
#	define __SSE4_2__
#endif
#endif
// https://github.com/llvm/llvm-project/commit/092d42557b6c70d32b0b9362e4a8db9566369ddc says this was an accidental omission, so this should do no harm
#if defined(DEATH_TARGET_CLANG) && __clang_major__ < 7
#pragma push_macro("__POPCNT__")
#if !defined(__POPCNT__)
#	define __POPCNT__
#endif
#endif
#include <smmintrin.h>
#include <nmmintrin.h>
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__*100 + __GNUC_MINOR__ < 409
#pragma pop_macro("__SSE4_1__")
#pragma pop_macro("__SSE4_2__")
#pragma GCC pop_options
#endif
#if defined(DEATH_TARGET_CLANG) && __clang_major__ < 7
#pragma pop_macro("__POPCNT__")
#endif